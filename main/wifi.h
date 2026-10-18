#pragma once
#include <stdbool.h>

void wifi_start(void);                         // start verbinden (op de achtergrond)
bool wifi_wait_connected(int timeout_ms);      // wacht tot er een IP-adres is
bool wifi_is_connected(void);
