/*
 * credentials.example.h — VOORBEELD, bevat geen echte gegevens
 *
 * Kopieer dit bestand naar credentials.h (in dezelfde map) en vul je eigen
 * gegevens in:   cp main/credentials.example.h main/credentials.h
 *
 * credentials.h staat in .gitignore en komt dus nooit op GitHub.
 */
#pragma once

/* Je Wi-Fi-netwerk (alleen 2,4 GHz; de ESP32-S3 ondersteunt geen 5 GHz) */
#define WIFI_SSID       "jouw-wifi-naam"
#define WIFI_PASSWORD   "jouw-wifi-wachtwoord"

/* Je OpenWeatherMap API-key: https://home.openweathermap.org/api_keys
 * Een nieuwe key kan een paar uur duren voordat hij werkt (tot die tijd: HTTP 401). */
#define OWM_API_KEY     "plak-hier-je-api-key"

/* Locatie: Breda, Nederland */
#define WEATHER_CITY    "Breda"
#define WEATHER_LAT     "51.5719"
#define WEATHER_LON     "4.7683"

/* Hoe vaak het weer opnieuw wordt opgehaald (in minuten) */
#define WEATHER_REFRESH_MIN  10
