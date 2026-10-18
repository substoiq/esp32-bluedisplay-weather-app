# Breda Weer — weerstation voor de Waveshare ESP32-S3-Touch-LCD-3.49 (V2)

Haalt elke 10 minuten het actuele weer en de verwachting voor Breda op bij
OpenWeatherMap en toont die liggend (640 × 172) in de stijl van GitHub.
Tik op het scherm om meteen te vernieuwen.

![Voorbeeld](preview.png)

> Alleen voor **V2** van het bord (opdruk "Rev1.1" op de print).
> V1 heeft andere pinnen en werkt niet met deze code.

## Wat heb je nodig

- Het Waveshare-bord (V2) en een **USB-C-kabel die data doet** (niet alleen laden)
- Een pc met Windows, macOS of Linux
- [VS Code](https://code.visualstudio.com/) met de extensie **"ESP-IDF"** van Espressif
- **ESP-IDF v5.5.3** (installeer je via de extensie, zie hieronder)
- Een gratis OpenWeatherMap-account en API-key
- Een 2,4 GHz Wi-Fi-netwerk

## 1. Eenmalig: ESP-IDF installeren

1. Installeer VS Code en open de Extensions-weergave (Ctrl+Shift+X).
2. Zoek op **ESP-IDF**, installeer de extensie van Espressif Systems.
3. De setup-wizard start vanzelf (of: Ctrl+Shift+P → "ESP-IDF: Configure ESP-IDF Extension").
4. Kies **Express**, selecteer versie **v5.5.3** en klik Install. Dit duurt even
   (enkele GB download).
5. Windows: de USB-driver is niet nodig; de ESP32-S3 heeft ingebouwde USB.

## 2. API-key en Wi-Fi invullen

Maak een account op <https://home.openweathermap.org/users/sign_up> en kopieer
je key van <https://home.openweathermap.org/api_keys>.

Kopieer **`main/credentials.example.h`** naar **`main/credentials.h`** en vul in:

```c
#define WIFI_SSID       "jouw-wifi-naam"
#define WIFI_PASSWORD   "jouw-wifi-wachtwoord"
#define OWM_API_KEY     "plak-hier-je-api-key"
```

Een nieuwe API-key kan tot een paar uur nodig hebben om actief te worden.
Tot die tijd toont het scherm een rood bolletje met "HTTP 401".

## 3. Bouwen en uploaden

1. Open de map `esp32-bluedisplay-weather-app` in VS Code (File → Open Folder).
2. Sluit het bord aan via USB-C.
3. Kies onderaan in de blauwe statusbalk:
   - **Target**: `esp32s3` (kies "ESP32-S3 chip (via builtin USB-JTAG)")
   - **Poort**: de COM-poort / `/dev/ttyACM0` / `/dev/cu.usbmodem…` van het bord
   - **Flash-methode**: UART
4. Klik op het **vlammetje** (Build, Flash and Monitor).

De eerste build duurt een paar minuten: LVGL en de schermdrivers worden dan
automatisch gedownload naar `managed_components/`. Daarna gaat het veel sneller.

Liever de commandline? Open een "ESP-IDF Terminal" (Ctrl+Shift+P) en typ:

```
idf.py set-target esp32s3
idf.py build flash monitor
```

Stoppen met de monitor: **Ctrl+]**.

**Uploaden lukt niet?** Zet het bord handmatig in downloadmodus: houd de
**BOOT**-knop (achterkant) ingedrukt, druk kort op **RESET**, laat BOOT los en
probeer opnieuw. Druk na het flashen nog één keer op RESET.

## Hoe het programma werkt

```
app_main()                       main.c
 ├─ display_init()               display_port.c  scherm, touch, LVGL-taak
 ├─ ui_create()                  ui.c            scherm opbouwen
 ├─ wifi_start()                 wifi.c          verbinden op de achtergrond
 └─ weather_task (eigen taak)    main.c
      lus: wacht op Wi-Fi → weather_fetch() → ui_update() → 10 min wachten
                                 weather.c       HTTPS + JSON uitpakken
```

Er lopen twee taken tegelijk: de **LVGL-taak** (tekent het scherm) en de
**weer-taak** (haalt data op). LVGL mag maar door één taak tegelijk gebruikt
worden, daarom staat elke UI-aanroep vanuit de weer-taak tussen
`display_lock()` en `display_unlock()`.

| Bestand | Wat zit erin | Aanpassen? |
|---|---|---|
| `main/credentials.h` | Wi-Fi, API-key, locatie, verversingstijd | **Ja** |
| `main/ui.c` | Het scherm: indeling, kleuren, teksten | Naar smaak |
| `main/weather.c` | API-aanroep en JSON uitpakken | Als je meer data wilt |
| `main/wifi.c` | Wi-Fi-verbinding | Nee |
| `main/main.c` | Opstarten en de weer-taak | Nee |
| `main/display_port.c` | Scherm-/touchdriver (uit Waveshare-voorbeeld) | Nee |
| `main/user_config.h` | Pinnen van het bord (Waveshare V2) | Nee |
| `components/` | Waveshare-hulpcode voor I2C en backlight | Nee |
| `sdkconfig.defaults` | Instellingen (PSRAM, fonts, HTTPS) | Zelden |

## Aanpassen

- **Licht thema**: zet in `main/ui.c` `#define UI_DARK_THEME` op `0`.
- **Scherm op z'n kop**: zet in `main/display_port.h` de rotatie op `LV_DISPLAY_ROTATION_270`.
- **Andere stad**: pas `WEATHER_CITY`, `WEATHER_LAT` en `WEATHER_LON` aan in `credentials.h`.
- **Andere lettergrootte**: alleen fonts die in `sdkconfig.defaults` aanstaan
  (12, 14, 16, 20, 48) zijn beschikbaar. Voeg je er een toe, verwijder dan het
  bestand `sdkconfig` zodat de instellingen opnieuw worden ingelezen.

## Problemen oplossen

| Op het scherm / in de monitor | Oorzaak |
|---|---|
| Zwart scherm | Verkeerde hardwareversie (V1?), of de upload is niet gelukt |
| "Geen Wi-Fi: controleer credentials.h" | Verkeerde SSID/wachtwoord, of 5 GHz-netwerk |
| "HTTP 401: API-key ongeldig..." | Key verkeerd overgenomen, of nog niet actief |
| "Geen verbinding met API" | Internet of DNS werkt niet via dit netwerk |
| Tekst staat op z'n kop | Rotatie op 270 zetten (zie boven) |

In de monitor (onderaan in VS Code) zie je logregels zoals
`I (5123) wifi: Verbonden, IP-adres: 192.168.1.42` die precies laten zien waar
het misgaat.
