/*
 * display_port.c — koppeling tussen het scherm/touch en LVGL
 *
 * Dit bestand is overgenomen uit het Waveshare V2-voorbeeld "10_LVGL_V9_Test"
 * en alleen opgeschoond: de demo is eruit, en de functies zijn naar buiten
 * beschikbaar gemaakt via display_port.h.
 *
 * Je hoeft hier normaal gesproken NIETS aan te veranderen. Het regelt:
 *   - de IO-expander (TCA9554) die op V2 de LCD-reset en backlight bedient
 *   - de QSPI-bus naar de AXS15231B-displaychip
 *   - LVGL: tekenbuffers, software-rotatie naar liggend, touch-invoer
 *   - een FreeRTOS-taak die LVGL laat draaien
 */
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_io_expander_tca9554.h"

#include "lvgl.h"
#include "esp_lcd_axs15231b.h"
#include "user_config.h"
#include "i2c_bsp.h"
#include "lcd_bl_pwm_bsp.h"
#include "display_port.h"

static const char *TAG = "display";

static SemaphoreHandle_t lvgl_mux = NULL;
static SemaphoreHandle_t flush_done_semaphore = NULL;
static uint8_t *lvgl_dest = NULL; // buffer voor het gedraaide beeld
static uint16_t *trans_buf_1;     // DMA-buffer richting het scherm
static esp_io_expander_handle_t io_expander = NULL;

#define LCD_BIT_PER_PIXEL 16
#define BYTES_PER_PIXEL (LV_COLOR_FORMAT_GET_SIZE(LV_COLOR_FORMAT_RGB565))
#define BUFF_SIZE (EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES * BYTES_PER_PIXEL)

#define LVGL_TICK_PERIOD_MS 5
#define LVGL_TASK_MAX_DELAY_MS 500
#define LVGL_TASK_MIN_DELAY_MS 10
#define LVGL_TASK_STACK_SIZE (8 * 1024)
#define LVGL_TASK_PRIORITY 2

static const axs15231b_lcd_init_cmd_t lcd_init_cmds[] = {
    {0x11, (uint8_t[]){0x00}, 0, 100},
    {0x29, (uint8_t[]){0x00}, 0, 100},
};

static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io,
                                    esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    BaseType_t high_task_awoken = pdFALSE;
    xSemaphoreGiveFromISR(flush_done_semaphore, &high_task_awoken);
    return false;
}

static void lcd_exio_init(void)
{
    i2c_master_bus_handle_t tca9554_i2c_bus = NULL;
    ESP_ERROR_CHECK(i2c_master_get_bus_handle(0, &tca9554_i2c_bus));
    ESP_ERROR_CHECK(esp_io_expander_new_i2c_tca9554(tca9554_i2c_bus, ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000, &io_expander));
    /* Accuvoeding vasthouden: zonder SYS_EN hoog valt het bord uit
     * zodra de USB-kabel eruit gaat. */
    ESP_ERROR_CHECK(esp_io_expander_set_dir(io_expander, EXAMPLE_EXIO_PIN_SYS_EN, IO_EXPANDER_OUTPUT));
    ESP_ERROR_CHECK(esp_io_expander_set_level(io_expander, EXAMPLE_EXIO_PIN_SYS_EN, 1));
    ESP_ERROR_CHECK(esp_io_expander_set_dir(io_expander, EXAMPLE_EXIO_PIN_TOUCH_INT, IO_EXPANDER_INPUT));
    ESP_ERROR_CHECK(esp_io_expander_set_dir(io_expander, EXAMPLE_EXIO_PIN_BL_EN | EXAMPLE_EXIO_PIN_LCD_RST, IO_EXPANDER_OUTPUT));
    ESP_ERROR_CHECK(esp_io_expander_set_level(io_expander, EXAMPLE_EXIO_PIN_BL_EN, 0));
    ESP_ERROR_CHECK(esp_io_expander_set_level(io_expander, EXAMPLE_EXIO_PIN_LCD_RST, 1));
}

static void lcd_reset(void)
{
    ESP_ERROR_CHECK(esp_io_expander_set_level(io_expander, EXAMPLE_EXIO_PIN_LCD_RST, 1));
    vTaskDelay(pdMS_TO_TICKS(30));
    ESP_ERROR_CHECK(esp_io_expander_set_level(io_expander, EXAMPLE_EXIO_PIN_LCD_RST, 0));
    vTaskDelay(pdMS_TO_TICKS(250));
    ESP_ERROR_CHECK(esp_io_expander_set_level(io_expander, EXAMPLE_EXIO_PIN_LCD_RST, 1));
    vTaskDelay(pdMS_TO_TICKS(30));
}

void display_backlight(bool on)
{
    ESP_ERROR_CHECK(esp_io_expander_set_level(io_expander, EXAMPLE_EXIO_PIN_BL_EN, on ? 1 : 0));
}

/* LVGL roept dit aan als er een nieuw beeld klaar is. Het beeld wordt (indien
 * liggend) eerst gedraaid en daarna in stukken via DMA naar het scherm gestuurd. */
static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *color_p)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);
    lv_draw_sw_rgb565_swap(color_p, lv_area_get_width(area) * lv_area_get_height(area));

    uint8_t *src = color_p;
    lv_display_rotation_t rotation = lv_display_get_rotation(disp);
    if (rotation != LV_DISPLAY_ROTATION_0)
    {
        lv_color_format_t cf = lv_display_get_color_format(disp);
        lv_area_t rotated_area = *area;
        lv_display_rotate_area(disp, &rotated_area);
        uint32_t src_stride = lv_draw_buf_width_to_stride(lv_area_get_width(area), cf);
        uint32_t dest_stride = lv_draw_buf_width_to_stride(lv_area_get_width(&rotated_area), cf);
        lv_draw_sw_rotate(color_p, lvgl_dest, lv_area_get_width(area), lv_area_get_height(area),
                          src_stride, dest_stride, rotation, cf);
        src = lvgl_dest;
    }

    const int flush_count = (LVGL_SPIRAM_BUFF_LEN / LVGL_DMA_BUFF_LEN);
    const int offgap = (EXAMPLE_LCD_V_RES / flush_count);
    const int dmalen = (LVGL_DMA_BUFF_LEN / 2);
    int y1 = 0;
    int y2 = offgap;

    uint16_t *map = (uint16_t *)src;
    xSemaphoreGive(flush_done_semaphore);
    for (int i = 0; i < flush_count; i++)
    {
        xSemaphoreTake(flush_done_semaphore, portMAX_DELAY);
        memcpy(trans_buf_1, map, LVGL_DMA_BUFF_LEN);
        esp_lcd_panel_draw_bitmap(panel_handle, 0, y1, EXAMPLE_LCD_H_RES, y2, trans_buf_1);
        y1 += offgap;
        y2 += offgap;
        map += dmalen;
    }
    xSemaphoreTake(flush_done_semaphore, portMAX_DELAY);
    lv_display_flush_ready(disp);
}

/* Touch uitlezen. We geven de coördinaten in de "eigen" (staande) oriëntatie
 * van het paneel door; LVGL 9 draait ze zelf mee als het scherm gedraaid is. */
static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    uint8_t read_touchpad_cmd[11] = {0xb5, 0xab, 0xa5, 0x5a, 0x0, 0x0, 0x0, 0x0e, 0x0, 0x0, 0x0};
    uint8_t buff[32] = {0};
    ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_master_write_read_dev(disp_touch_dev_handle, read_touchpad_cmd, 11, buff, 32));
    uint16_t px = (((uint16_t)buff[2] & 0x0f) << 8) | (uint16_t)buff[3];
    uint16_t py = (((uint16_t)buff[4] & 0x0f) << 8) | (uint16_t)buff[5];

    if (buff[1] > 0 && buff[1] < 5)
    {
        if (px > EXAMPLE_LCD_V_RES)
            px = EXAMPLE_LCD_V_RES;
        if (py > EXAMPLE_LCD_H_RES)
            py = EXAMPLE_LCD_H_RES;
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = py;
        data->point.y = EXAMPLE_LCD_V_RES - px;
    }
    else
    {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void increase_lvgl_tick(void *arg)
{
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

bool display_lock(int timeout_ms)
{
    const TickType_t ticks = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(lvgl_mux, ticks) == pdTRUE;
}

void display_unlock(void)
{
    xSemaphoreGive(lvgl_mux);
}

/* De "hoofdlus" van LVGL, als aparte FreeRTOS-taak */
static void lvgl_port_task(void *arg)
{
    uint32_t delay_ms = LVGL_TASK_MAX_DELAY_MS;
    for (;;)
    {
        if (display_lock(-1))
        {
            delay_ms = lv_timer_handler();
            display_unlock();
        }
        if (delay_ms > LVGL_TASK_MAX_DELAY_MS)
            delay_ms = LVGL_TASK_MAX_DELAY_MS;
        if (delay_ms < LVGL_TASK_MIN_DELAY_MS)
            delay_ms = LVGL_TASK_MIN_DELAY_MS;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

void display_init(void)
{
    lcd_bl_pwm_bsp_init(LCD_PWM_MODE_255);
    flush_done_semaphore = xSemaphoreCreateBinary();
    assert(flush_done_semaphore);
    touch_i2c_master_Init();
    lcd_exio_init();

    ESP_LOGI(TAG, "SPI-bus initialiseren");
    spi_bus_config_t buscfg = {};
    buscfg.sclk_io_num = EXAMPLE_PIN_NUM_LCD_PCLK;
    buscfg.data0_io_num = EXAMPLE_PIN_NUM_LCD_DATA0;
    buscfg.data1_io_num = EXAMPLE_PIN_NUM_LCD_DATA1;
    buscfg.data2_io_num = EXAMPLE_PIN_NUM_LCD_DATA2;
    buscfg.data3_io_num = EXAMPLE_PIN_NUM_LCD_DATA3;
    buscfg.max_transfer_sz = LVGL_DMA_BUFF_LEN;
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t panel_io = NULL;
    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.cs_gpio_num = EXAMPLE_PIN_NUM_LCD_CS;
    io_config.dc_gpio_num = -1;
    io_config.spi_mode = 3;
    io_config.pclk_hz = 40 * 1000 * 1000;
    io_config.trans_queue_depth = 10;
    io_config.on_color_trans_done = notify_lvgl_flush_ready;
    io_config.lcd_cmd_bits = 32;
    io_config.lcd_param_bits = 8;
    io_config.flags.quad_mode = true;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(LCD_HOST, &io_config, &panel_io));

    axs15231b_vendor_config_t vendor_config = {};
    vendor_config.flags.use_qspi_interface = 1;
    vendor_config.init_cmds = lcd_init_cmds;
    vendor_config.init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]);

    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = -1;
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_config.bits_per_pixel = LCD_BIT_PER_PIXEL;
    panel_config.vendor_config = &vendor_config;
    ESP_ERROR_CHECK(esp_lcd_new_panel_axs15231b(panel_io, &panel_config, &panel));

    lcd_reset();
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));

    ESP_LOGI(TAG, "LVGL initialiseren");
    lv_init();
    lv_display_t *disp = lv_display_create(EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES);
    lv_display_set_flush_cb(disp, lvgl_flush_cb);

    uint8_t *buffer_1 = heap_caps_malloc(BUFF_SIZE, MALLOC_CAP_SPIRAM);
    uint8_t *buffer_2 = heap_caps_malloc(BUFF_SIZE, MALLOC_CAP_SPIRAM);
    trans_buf_1 = heap_caps_malloc(LVGL_DMA_BUFF_LEN, MALLOC_CAP_DMA);
    lvgl_dest = heap_caps_malloc(BUFF_SIZE, MALLOC_CAP_SPIRAM);
    assert(buffer_1 && buffer_2 && trans_buf_1 && lvgl_dest);
    lv_display_set_buffers(disp, buffer_1, buffer_2, BUFF_SIZE, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_user_data(disp, panel);
    lv_display_set_rotation(disp, DISPLAY_ROTATION); // liggend: zie display_port.h

    lv_indev_t *touch = lv_indev_create();
    lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch, touch_read_cb);

    esp_timer_create_args_t tick_args = {
        .callback = &increase_lvgl_tick,
        .name = "lvgl_tick",
    };
    esp_timer_handle_t tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, LVGL_TICK_PERIOD_MS * 1000));

    lvgl_mux = xSemaphoreCreateMutex();
    assert(lvgl_mux);
    xTaskCreatePinnedToCore(lvgl_port_task, "LVGL", LVGL_TASK_STACK_SIZE, NULL, LVGL_TASK_PRIORITY, NULL, 0);
}
