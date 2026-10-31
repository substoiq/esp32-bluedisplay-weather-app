/*
 * main.c — het startpunt van het programma
 *
 * In ESP-IDF begint alles in app_main() (vergelijkbaar met main.py op een Pico).
 * Hier zetten we de onderdelen aan en starten we een aparte FreeRTOS-taak die
 * elke paar minuten het weer ophaalt. Ondertussen draait LVGL in zijn eigen taak
 * (zie display_port.c). Twee taken die tegelijk lopen dus — vandaar het "lock".
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include "credentials.h"
#include "display_port.h"
#include "wifi.h"
#include "weather.h"
#include "ui.h"
#include "battery.h"

static const char *TAG = "main";
static TaskHandle_t weather_task_handle;

/* Wordt aangeroepen als je op het scherm tikt (vanuit de LVGL-taak).
 * We "porren" de weer-taak, zodat die meteen opnieuw ophaalt. */
static void on_screen_tap(void)
{
    if (weather_task_handle != NULL) {
        xTaskNotifyGive(weather_task_handle);
    }
}

/* Toon een status in de kopbalk; veilig aan te roepen vanuit deze taak */
static void show_status(ui_status_t status, const char *text)
{
    if (display_lock(-1)) {
        ui_set_status(status, text);
        display_unlock();
    }
}

/* De weer-taak: een oneindige lus, net als 'while True:' in MicroPython */
static void weather_task(void *arg)
{
    /* 'static' binnen een functie: blijft bestaan (en staat niet op de
     * kleine stack van de taak, maar in het gewone geheugen) */
    static weather_t weather;
    char error[64];

    for (;;) {
        if (!wifi_wait_connected(15000)) {
            show_status(UI_STATUS_ERROR, "Geen Wi-Fi: controleer credentials.h");
            continue;                        // opnieuw wachten
        }

        show_status(UI_STATUS_LOADING, "Weer ophalen...");
        if (weather_fetch(&weather, error, sizeof(error))) {
            if (display_lock(-1)) {
                ui_update(&weather);
                display_unlock();
            }
        } else {
            ESP_LOGE(TAG, "Ophalen mislukt: %s", error);
            show_status(UI_STATUS_ERROR, error);
        }

        /* Wacht tot de volgende ronde, OF tot iemand op het scherm tikt */
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(WEATHER_REFRESH_MIN * 60 * 1000));
    }
}

void app_main(void)
{
    /* NVS = klein stukje flash waar de Wi-Fi-driver instellingen bewaart */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    display_init();
    battery_init();                          // vóór ui_create: die meet meteen de accu

    if (display_lock(-1)) {
        ui_create(on_screen_tap);
        display_unlock();
    }
    vTaskDelay(pdMS_TO_TICKS(100));          // eerste beeld laten tekenen...
    display_backlight(true);                 // ...en dan pas het licht aan

    wifi_start();

    /* Taak starten: functie, naam, stackgrootte (bytes), parameter, prioriteit, handle.
     * HTTPS heeft vrij veel stack nodig, vandaar 12 kB. */
    xTaskCreate(weather_task, "weather", 12 * 1024, NULL, 3, &weather_task_handle);
}
