/*
 * battery.c — accuspanning meten
 *
 * Op het Waveshare-bord gaat de accuspanning via een spanningsdeler (÷3) naar
 * GPIO4 = ADC1 kanaal 3 (zie Waveshare-voorbeeld 01_ADC_Test).
 * We meten dus 1/3 van de accuspanning en vermenigvuldigen weer met 3.
 */
#include <stdbool.h>
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"
#include "battery.h"

#define BAT_ADC_UNIT     ADC_UNIT_1
#define BAT_ADC_CHANNEL  ADC_CHANNEL_3
#define BAT_DIVIDER      3
#define BAT_SAMPLES      8           // gemiddelde van 8 metingen tegen ruis

static adc_oneshot_unit_handle_t adc_handle;
static adc_cali_handle_t cali_handle;
static bool ready = false;

void battery_init(void)
{
    adc_oneshot_unit_init_cfg_t unit_cfg = { .unit_id = BAT_ADC_UNIT };
    if (adc_oneshot_new_unit(&unit_cfg, &adc_handle) != ESP_OK) return;

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,        // meetbereik tot ca. 3,1 V
        .bitwidth = ADC_BITWIDTH_12,
    };
    adc_oneshot_config_channel(adc_handle, BAT_ADC_CHANNEL, &chan_cfg);

    // Kalibratie: ruwe ADC-waarde -> echte millivolts
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = BAT_ADC_UNIT,
        .chan = BAT_ADC_CHANNEL,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &cali_handle) != ESP_OK) return;
    ready = true;
}

int battery_read_mv(void)
{
    if (!ready) return 0;
    int sum = 0, count = 0;
    for (int i = 0; i < BAT_SAMPLES; i++) {
        int raw, mv;
        if (adc_oneshot_read(adc_handle, BAT_ADC_CHANNEL, &raw) == ESP_OK &&
            adc_cali_raw_to_voltage(cali_handle, raw, &mv) == ESP_OK) {
            sum += mv;
            count++;
        }
    }
    return count ? (sum / count) * BAT_DIVIDER : 0;
}

/* Een Li-ion-accu is niet lineair: tussen 3,7 en 3,9 V zit het grootste deel
 * van de lading. Deze tabel geeft een redelijke schatting; ertussen rekenen we
 * lineair (interpoleren). */
int battery_percent(int mv)
{
    static const int table[][2] = {
        {4200, 100}, {4100, 90}, {4000, 80}, {3900, 65}, {3800, 50},
        {3700, 35},  {3600, 20}, {3500, 10}, {3400, 5},  {3300, 0},
    };
    const int n = sizeof(table) / sizeof(table[0]);

    if (mv >= table[0][0]) return 100;
    if (mv <= table[n - 1][0]) return 0;
    for (int i = 1; i < n; i++) {
        if (mv >= table[i][0]) {
            int mv_hi = table[i - 1][0], p_hi = table[i - 1][1];
            int mv_lo = table[i][0],     p_lo = table[i][1];
            return p_lo + (mv - mv_lo) * (p_hi - p_lo) / (mv_hi - mv_lo);
        }
    }
    return 0;
}
