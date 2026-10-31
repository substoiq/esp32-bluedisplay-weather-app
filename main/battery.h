#pragma once

void battery_init(void);       // ADC klaarzetten (één keer, bij het opstarten)
int  battery_read_mv(void);    // accuspanning in millivolt, bijv. 3950
int  battery_percent(int mv);  // spanning -> geschatte lading 0..100 %
