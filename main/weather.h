#pragma once
#include <stdbool.h>
#include <stdint.h>

#define FORECAST_COUNT 4      // aantal verwachtingen (elk 3 uur verder)

/* Eén voorspelling, bijvoorbeeld "21:00, 12°, lichte regen" */
typedef struct {
    int64_t dt;               // tijdstip (Unix-tijd, UTC)
    float   temp;             // °C
    int     pop;              // kans op neerslag in %
    char    main[16];         // hoofdcategorie in het Engels: "Rain", "Clouds", ...
    char    description[48];  // omschrijving in het Nederlands
} forecast_t;

/* Alles wat we op het scherm laten zien.
 * Een struct is vergelijkbaar met een Python-dataclass. */
typedef struct {
    float   temp, feels_like, temp_min, temp_max;
    int     humidity;         // %
    int     pressure;         // hPa
    float   wind_kmh;
    int     wind_deg;
    int64_t dt, sunrise, sunset;
    int     tz_offset;        // tijdzone-verschil met UTC in seconden
    char    main[16];
    char    description[48];
    forecast_t forecast[FORECAST_COUNT];
    int     forecast_count;
} weather_t;

/* Haalt het actuele weer en de verwachting op.
 * Geeft true terug als het gelukt is. Bij een fout staat er een korte
 * omschrijving in 'error' (bijv. "HTTP 401: API-key ongeldig"). */
bool weather_fetch(weather_t *out, char *error, int error_len);
