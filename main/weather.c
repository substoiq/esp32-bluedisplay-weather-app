/*
 * weather.c — weerdata ophalen bij OpenWeatherMap en uitpakken
 *
 * Stappen:
 *   1. Een HTTPS-verzoek doen (esp_http_client, zit in ESP-IDF)
 *   2. Het antwoord (JSON-tekst) uitpakken met cJSON (zit ook in ESP-IDF)
 *   3. De waarden in een weather_t-struct zetten
 *
 * We gebruiken twee gratis endpoints:
 *   /data/2.5/weather   -> het weer van nu
 *   /data/2.5/forecast  -> verwachting in stappen van 3 uur
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "cJSON.h"

#include "weather.h"
#include "credentials.h"

static const char *TAG = "weather";

#define HTTP_BUF_SIZE  (16 * 1024)   // ruim genoeg voor beide antwoorden

/* Doet een GET-verzoek en zet het antwoord in 'buf'.
 * Geeft de HTTP-statuscode terug (200 = OK), of -1 bij een netwerkfout. */
static int http_get(const char *url, char *buf, int buf_size)
{
    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,   // HTTPS-certificaten controleren
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) return -1;

    int status = -1;
    if (esp_http_client_open(client, 0) == ESP_OK) {
        esp_http_client_fetch_headers(client);
        int len = esp_http_client_read_response(client, buf, buf_size - 1);
        buf[len > 0 ? len : 0] = '\0';                 // C-strings eindigen met een 0-byte
        status = esp_http_client_get_status_code(client);
    }
    esp_http_client_cleanup(client);
    return status;
}

/* Kleine hulpfuncties om veilig een getal of tekst uit JSON te lezen.
 * Bestaat het veld niet, dan krijg je de standaardwaarde terug. */
static double json_num(const cJSON *obj, const char *key, double fallback)
{
    const cJSON *item = cJSON_GetObjectItem(obj, key);
    return cJSON_IsNumber(item) ? item->valuedouble : fallback;
}

static void json_str(const cJSON *obj, const char *key, char *dest, size_t dest_len)
{
    const cJSON *item = cJSON_GetObjectItem(obj, key);
    snprintf(dest, dest_len, "%s", cJSON_IsString(item) ? item->valuestring : "");
}

/* "weather" is in de JSON een lijst; we nemen het eerste element */
static void parse_condition(const cJSON *parent, char *main, size_t main_len,
                            char *desc, size_t desc_len)
{
    const cJSON *first = cJSON_GetArrayItem(cJSON_GetObjectItem(parent, "weather"), 0);
    json_str(first, "main", main, main_len);
    json_str(first, "description", desc, desc_len);
}

static void error_for_status(int status, char *error, int error_len)
{
    if (status == -1)       snprintf(error, error_len, "Geen verbinding met API");
    else if (status == 401) snprintf(error, error_len, "HTTP 401: API-key ongeldig of nog niet actief");
    else if (status == 429) snprintf(error, error_len, "HTTP 429: te veel aanroepen");
    else                    snprintf(error, error_len, "HTTP %d", status);
}

bool weather_fetch(weather_t *out, char *error, int error_len)
{
    char url[256];
    // Geheugen voor het antwoord nemen uit het grote PSRAM (8 MB)
    char *buf = heap_caps_malloc(HTTP_BUF_SIZE, MALLOC_CAP_SPIRAM);
    if (buf == NULL) {
        snprintf(error, error_len, "Geen geheugen");
        return false;
    }
    bool ok = false;
    cJSON *root = NULL;

    /* ---- 1. Het weer van nu ---- */
    snprintf(url, sizeof(url),
             "https://api.openweathermap.org/data/2.5/weather?lat=%s&lon=%s&units=metric&lang=nl&appid=%s",
             WEATHER_LAT, WEATHER_LON, OWM_API_KEY);
    int status = http_get(url, buf, HTTP_BUF_SIZE);
    if (status != 200) {
        error_for_status(status, error, error_len);
        goto done;      // in C is 'goto' naar een opruimstap een gangbaar patroon
    }

    root = cJSON_Parse(buf);
    if (root == NULL) {
        snprintf(error, error_len, "Ongeldige JSON van API");
        goto done;
    }
    const cJSON *m = cJSON_GetObjectItem(root, "main");
    out->temp       = json_num(m, "temp", 0);
    out->feels_like = json_num(m, "feels_like", 0);
    out->temp_min   = json_num(m, "temp_min", 0);
    out->temp_max   = json_num(m, "temp_max", 0);
    out->humidity   = json_num(m, "humidity", 0);
    out->pressure   = json_num(m, "pressure", 0);

    const cJSON *wind = cJSON_GetObjectItem(root, "wind");
    out->wind_kmh = json_num(wind, "speed", 0) * 3.6;    // m/s -> km/h
    out->wind_deg = json_num(wind, "deg", 0);

    const cJSON *sys = cJSON_GetObjectItem(root, "sys");
    out->sunrise   = json_num(sys, "sunrise", 0);
    out->sunset    = json_num(sys, "sunset", 0);
    out->dt        = json_num(root, "dt", 0);
    out->tz_offset = json_num(root, "timezone", 0);
    parse_condition(root, out->main, sizeof(out->main), out->description, sizeof(out->description));

    cJSON_Delete(root);      // in C ruim je zelf op: geen garbage collector!
    root = NULL;

    /* ---- 2. De verwachting ---- */
    snprintf(url, sizeof(url),
             "https://api.openweathermap.org/data/2.5/forecast?lat=%s&lon=%s&units=metric&lang=nl&cnt=%d&appid=%s",
             WEATHER_LAT, WEATHER_LON, FORECAST_COUNT, OWM_API_KEY);
    status = http_get(url, buf, HTTP_BUF_SIZE);
    out->forecast_count = 0;
    if (status == 200 && (root = cJSON_Parse(buf)) != NULL) {
        const cJSON *list = cJSON_GetObjectItem(root, "list");
        const cJSON *item;
        cJSON_ArrayForEach(item, list) {               // ~ for item in list:
            if (out->forecast_count >= FORECAST_COUNT) break;
            forecast_t *f = &out->forecast[out->forecast_count++];
            f->dt   = json_num(item, "dt", 0);
            f->temp = json_num(cJSON_GetObjectItem(item, "main"), "temp", 0);
            f->pop  = (int)(json_num(item, "pop", 0) * 100 + 0.5);
            parse_condition(item, f->main, sizeof(f->main), f->description, sizeof(f->description));
        }
    } else {
        ESP_LOGW(TAG, "Verwachting ophalen mislukt (status %d)", status);
    }

    ESP_LOGI(TAG, "%s: %.1f C, %s, %d verwachtingen",
             WEATHER_CITY, out->temp, out->description, out->forecast_count);
    ok = true;

done:
    cJSON_Delete(root);      // mag ook met NULL
    free(buf);
    return ok;
}
