#pragma once
#include <stdbool.h>

/* Liggend scherm (640 x 172).
 * Staat de tekst op z'n kop? Verander 90 dan in 270. */
#define DISPLAY_ROTATION  LV_DISPLAY_ROTATION_90

void display_init(void);               // scherm, touch en LVGL opstarten
void display_backlight(bool on);       // achtergrondverlichting aan/uit

/* LVGL is niet thread-safe: roep deze twee aan rond elke LVGL-aanroep
 * die NIET vanuit een LVGL-callback komt (bijv. vanuit de weer-taak). */
bool display_lock(int timeout_ms);     // -1 = wacht zo lang als nodig
void display_unlock(void);
