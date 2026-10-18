#pragma once
#include "weather.h"

typedef enum {
    UI_STATUS_LOADING,   // geel bolletje, zoals een lopende GitHub-check
    UI_STATUS_OK,        // groen
    UI_STATUS_ERROR,     // rood
} ui_status_t;

/* Bouwt het scherm op. 'on_tap' wordt aangeroepen als je op het scherm tikt. */
void ui_create(void (*on_tap)(void));

void ui_update(const weather_t *w);                     // nieuwe weerdata tonen
void ui_set_status(ui_status_t status, const char *text);

/* Let op: ui_update en ui_set_status alleen aanroepen tussen
 * display_lock() en display_unlock(). */
