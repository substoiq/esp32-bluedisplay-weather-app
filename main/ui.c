/*
 * ui.c — het weerscherm, in de stijl van GitHub (Primer-ontwerpsysteem)
 *
 * Indeling (liggend, 640 x 172 pixels):
 *
 *  ┌──────────────────────────────────────────────────────────────────┐
 *  │ weer / breda  (OpenWeatherMap)                 ● Bijgewerkt 17:58 │  kopbalk
 *  ├──────────────────────────────────────┬───────────────────────────┤
 *  │ Nu                    min 9° · max 14°│ Verwachting               │
 *  │  14°         Gevoel    12°            │ 18:00  13°  lichte regen  │
 *  │              Wind      18 km/h ZW     │ 21:00  11°  bewolkt       │
 *  │ (lichte regen) Vocht   82%            │ 00:00  10°  helder        │
 *  │              Zon       07:51 / 19:12  │ 03:00   9°  helder        │
 *  └──────────────────────────────────────┴───────────────────────────┘
 *
 * Alle posities zijn in pixels, met (0,0) linksboven.
 */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <time.h>
#include "lvgl.h"
#include "ui.h"
#include "battery.h"
#include "credentials.h"

/* ================= Kleuren =================
 * GitHub heeft een donker en een licht thema. Zet op 0 voor licht. */
#define UI_DARK_THEME 1

#if UI_DARK_THEME
#define C_CANVAS 0x0d1117       // achtergrond
#define C_SUBTLE 0x161b22       // kopjes en kopbalk
#define C_BORDER 0x30363d       // randen
#define C_BORDER_MUTED 0x21262d // lijntjes tussen tabelrijen
#define C_FG 0xe6edf3           // gewone tekst
#define C_FG_MUTED 0x9198a1     // grijze tekst
#define C_ACCENT 0x4493f8       // blauw (links)
#define C_SUCCESS 0x3fb950      // groen
#define C_DANGER 0xf85149       // rood
#define C_ATTENTION 0xd29922    // geel
#define C_DONE 0xab7df8         // paars
#else
#define C_CANVAS 0xffffff
#define C_SUBTLE 0xf6f8fa
#define C_BORDER 0xd1d9e0
#define C_BORDER_MUTED 0xd8dee4
#define C_FG 0x1f2328
#define C_FG_MUTED 0x59636e
#define C_ACCENT 0x0969da
#define C_SUCCESS 0x1a7f37
#define C_DANGER 0xd1242f
#define C_ATTENTION 0x9a6700
#define C_DONE 0x8250df
#endif

#define DEG "\xC2\xB0" // het graden-teken ° in UTF-8

/* ================= Objecten die we later bijwerken ================= */
static lv_obj_t *now_title, *temp_label, *minmax_label, *cond_pill, *cond_pill_text;
static lv_obj_t *status_dot, *status_text;
static lv_obj_t *bat_label;
static lv_obj_t *stat_value[4];
static lv_obj_t *fc_time[FORECAST_COUNT], *fc_temp[FORECAST_COUNT];
static lv_obj_t *fc_desc[FORECAST_COUNT], *fc_pop[FORECAST_COUNT];
static void (*tap_callback)(void);

/* ================= Hulpfuncties voor de opbouw ================= */

/* Een kaal object zonder standaard-LVGL-opmaak, niet scrollbaar en niet
 * klikbaar (dan komt een tik altijd bij het scherm zelf terecht). */
static lv_obj_t *plain_obj(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color,
                            int x, int y, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_pos(l, x, y);
    lv_label_set_text(l, text);
    return l;
}

/* Een GitHub-"Box": rand, afgeronde hoeken en een grijze kopregel */
static lv_obj_t *make_box(lv_obj_t *parent, int x, int y, int w, int h,
                          const char *title, lv_obj_t **title_out, lv_obj_t **header_right)
{
    lv_obj_t *box = plain_obj(parent);
    lv_obj_set_pos(box, x, y);
    lv_obj_set_size(box, w, h);
    lv_obj_set_style_bg_color(box, lv_color_hex(C_CANVAS), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(box, lv_color_hex(C_BORDER), 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_radius(box, 6, 0);
    lv_obj_set_style_clip_corner(box, true, 0);

    lv_obj_t *head = plain_obj(box);
    lv_obj_set_size(head, w - 2, 28);
    lv_obj_set_style_bg_color(head, lv_color_hex(C_SUBTLE), 0);
    lv_obj_set_style_bg_opa(head, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(head, lv_color_hex(C_BORDER), 0);
    lv_obj_set_style_border_width(head, 1, 0);
    lv_obj_set_style_border_side(head, LV_BORDER_SIDE_BOTTOM, 0);

    lv_obj_t *t = make_label(head, &lv_font_montserrat_14, C_FG, 12, 0, title);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 12, 0);
    if (title_out)
        *title_out = t; // zodat we de titel later kunnen aanpassen

    if (header_right)
    {
        *header_right = make_label(head, &lv_font_montserrat_12, C_FG_MUTED, 0, 0, "");
        lv_obj_align(*header_right, LV_ALIGN_RIGHT_MID, -12, 0);
    }
    return box;
}

/* Een GitHub-"Label": afgerond pilletje met gekleurde rand en tekst */
static lv_obj_t *make_pill(lv_obj_t *parent, lv_obj_t **text_out)
{
    lv_obj_t *pill = plain_obj(parent);
    lv_obj_set_size(pill, LV_SIZE_CONTENT, 22);
    lv_obj_set_style_radius(pill, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(pill, 1, 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_20, 0);
    lv_obj_set_style_border_opa(pill, LV_OPA_60, 0);
    lv_obj_set_style_pad_hor(pill, 9, 0);

    *text_out = lv_label_create(pill);
    lv_obj_set_style_text_font(*text_out, &lv_font_montserrat_12, 0);
    lv_obj_align(*text_out, LV_ALIGN_LEFT_MID, 0, 0);
    return pill;
}

static void set_pill(lv_obj_t *pill, lv_obj_t *text, uint32_t color, const char *str)
{
    lv_color_t c = lv_color_hex(color);
    lv_obj_set_style_bg_color(pill, c, 0);
    lv_obj_set_style_border_color(pill, c, 0);
    lv_obj_set_style_text_color(text, c, 0);
    lv_label_set_text(text, str);
}

/* ================= Hulpfuncties voor de data ================= */

/* Kleur per weertype, net zoals GitHub labels kleurt */
static uint32_t color_for_condition(const char *main)
{
    if (strcmp(main, "Clear") == 0)
        return C_ATTENTION; // zon: geel
    if (strcmp(main, "Rain") == 0 ||
        strcmp(main, "Drizzle") == 0)
        return C_ACCENT; // regen: blauw
    if (strcmp(main, "Thunderstorm") == 0)
        return C_DONE; // onweer: paars
    if (strcmp(main, "Snow") == 0)
        return C_FG;   // sneeuw: wit
    return C_FG_MUTED; // bewolkt, mist: grijs
}

/* Unix-tijd + tijdzone -> "HH:MM" */
static void format_time(int64_t utc, int tz_offset, char *out, size_t len)
{
    time_t t = (time_t)(utc + tz_offset);
    struct tm tm;
    gmtime_r(&t, &tm);
    strftime(out, len, "%H:%M", &tm);
}

/* Unix-tijd + tijdzone -> "DD-MM-JJJJ" */
static void format_date(int64_t utc, int tz_offset, char *out, size_t len)
{
    time_t t = (time_t)(utc + tz_offset);
    struct tm tm;
    gmtime_r(&t, &tm);
    strftime(out, len, "%d-%m-%Y", &tm);
}

static const char *wind_direction(int deg)
{
    static const char *dirs[] = {"N", "NO", "O", "ZO", "Z", "ZW", "W", "NW"};
    return dirs[((deg + 22) / 45) % 8];
}

/* Eerste letter een hoofdletter: "lichte regen" -> "Lichte regen" */
static void capitalize(const char *in, char *out, size_t len)
{
    snprintf(out, len, "%s", in);
    if (out[0])
        out[0] = toupper((unsigned char)out[0]);
}

static int round_temp(float t)
{
    int r = (int)lroundf(t);
    return r == 0 ? 0 : r; // voorkomt "-0"
}

/* Accustatus bijwerken. Een lv_timer draait binnen de LVGL-taak, dus hier
 * is geen display_lock() nodig. */
static void battery_update(lv_timer_t *timer)
{
    int mv = battery_read_mv();
    if (mv == 0)
    {
        lv_label_set_text(bat_label, ""); // meting mislukt: niets tonen
        return;
    }
    int pct = battery_percent(mv);

    const char *symbol = pct > 80   ? LV_SYMBOL_BATTERY_FULL
                         : pct > 55 ? LV_SYMBOL_BATTERY_3
                         : pct > 30 ? LV_SYMBOL_BATTERY_2
                         : pct > 10 ? LV_SYMBOL_BATTERY_1
                                    : LV_SYMBOL_BATTERY_EMPTY;
    uint32_t color = pct > 50 ? C_SUCCESS : pct > 20 ? C_ATTENTION
                                                     : C_DANGER;

    lv_label_set_text_fmt(bat_label, "%s %d%%", symbol, pct);
    lv_obj_set_style_text_color(bat_label, lv_color_hex(color), 0);
}

static void screen_clicked(lv_event_t *e)
{
    if (tap_callback)
        tap_callback();
}

/* ================= Het scherm opbouwen ================= */

void ui_create(void (*on_tap)(void))
{
    tap_callback = on_tap;
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(C_CANVAS), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scr, screen_clicked, LV_EVENT_CLICKED, NULL);

    /* ---- Kopbalk ---- */
    lv_obj_t *bar = plain_obj(scr);
    lv_obj_set_size(bar, 640, 32);
    lv_obj_set_style_bg_color(bar, lv_color_hex(C_SUBTLE), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(bar, lv_color_hex(C_BORDER), 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);

    // Links: kruimelpad zoals "eigenaar / repository"
    lv_obj_t *crumb = plain_obj(bar);
    lv_obj_set_size(crumb, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(crumb, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(crumb, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(crumb, 6, 0);
    lv_obj_align(crumb, LV_ALIGN_LEFT_MID, 12, 0);

    make_label(crumb, &lv_font_montserrat_16, C_FG, 0, 0, "weer");
    make_label(crumb, &lv_font_montserrat_16, C_FG_MUTED, 0, 0, "/");
    char city[32];
    snprintf(city, sizeof(city), "%s", WEATHER_CITY);
    for (char *p = city; *p; p++)
        *p = tolower((unsigned char)*p);
    make_label(crumb, &lv_font_montserrat_16, C_ACCENT, 0, 0, city);

    lv_obj_t *src_text;
    lv_obj_t *src_pill = make_pill(crumb, &src_text);
    set_pill(src_pill, src_text, C_FG_MUTED, "OpenWeatherMap");
    lv_obj_set_style_bg_opa(src_pill, LV_OPA_TRANSP, 0); // GitHub "Public"-label: alleen rand
    lv_obj_set_style_margin_left(src_pill, 4, 0);

    // Accustatus, direct rechts van het OpenWeatherMap-label
    bat_label = make_label(crumb, &lv_font_montserrat_14, C_FG_MUTED, 0, 0, "");
    lv_obj_set_style_margin_left(bat_label, 14, 0);
    battery_update(NULL);                         // meteen een eerste meting
    lv_timer_create(battery_update, 30000, NULL); // en daarna elke 30 seconden

    // Rechts: statusbolletje en tekst, zoals een CI-check
    lv_obj_t *status = plain_obj(bar);
    lv_obj_set_size(status, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(status, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(status, 6, 0);
    lv_obj_align(status, LV_ALIGN_RIGHT_MID, -12, 0);

    status_dot = plain_obj(status);
    lv_obj_set_size(status_dot, 8, 8);
    lv_obj_set_style_radius(status_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(status_dot, LV_OPA_COVER, 0);

    status_text = make_label(status, &lv_font_montserrat_12, C_FG_MUTED, 0, 0, "");
    lv_obj_set_style_max_width(status_text, 300, 0);
    lv_label_set_long_mode(status_text, LV_LABEL_LONG_DOT);

    /* ---- Box 1: het weer van nu ---- */
    lv_obj_t *now = make_box(scr, 8, 40, 360, 124, "Nu", &now_title, &minmax_label);

    temp_label = make_label(now, &lv_font_montserrat_48, C_FG, 14, 34, "--" DEG);
    cond_pill = make_pill(now, &cond_pill_text);
    lv_obj_set_pos(cond_pill, 14, 92);
    set_pill(cond_pill, cond_pill_text, C_FG_MUTED, "Laden...");

    // Verticale scheidingslijn
    lv_obj_t *divider = plain_obj(now);
    lv_obj_set_pos(divider, 164, 38);
    lv_obj_set_size(divider, 1, 76);
    lv_obj_set_style_bg_color(divider, lv_color_hex(C_BORDER_MUTED), 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);

    static const char *stat_names[] = {"Gevoel", "Wind", "Vocht", "Zon"};
    for (int i = 0; i < 4; i++)
    {
        int y = 38 + i * 21;
        make_label(now, &lv_font_montserrat_14, C_FG_MUTED, 178, y, stat_names[i]);
        stat_value[i] = make_label(now, &lv_font_montserrat_14, C_FG, 246, y, "--");
    }

    /* ---- Box 2: de verwachting, als GitHub-tabel ---- */
    lv_obj_t *fc = make_box(scr, 376, 40, 256, 124, "Verwachting", NULL, NULL);

    for (int i = 0; i < FORECAST_COUNT; i++)
    {
        lv_obj_t *row = plain_obj(fc);
        lv_obj_set_pos(row, 0, 28 + i * 24);
        lv_obj_set_size(row, 254, 24);
        if (i > 0)
        {
            lv_obj_set_style_border_color(row, lv_color_hex(C_BORDER_MUTED), 0);
            lv_obj_set_style_border_width(row, 1, 0);
            lv_obj_set_style_border_side(row, LV_BORDER_SIDE_TOP, 0);
        }
        fc_time[i] = make_label(row, &lv_font_montserrat_12, C_FG_MUTED, 12, 0, "--:--");
        fc_temp[i] = make_label(row, &lv_font_montserrat_14, C_FG, 56, 0, "");
        fc_desc[i] = make_label(row, &lv_font_montserrat_12, C_FG, 96, 0, "");
        lv_obj_set_size(fc_desc[i], 112, 15);                  // vaste maat = één regel
        lv_label_set_long_mode(fc_desc[i], LV_LABEL_LONG_DOT); // te lang? dan "..."
        fc_pop[i] = make_label(row, &lv_font_montserrat_12, C_ACCENT, 0, 0, "");

        lv_obj_align(fc_time[i], LV_ALIGN_LEFT_MID, 12, 0);
        lv_obj_align(fc_temp[i], LV_ALIGN_LEFT_MID, 56, 0);
        lv_obj_align(fc_desc[i], LV_ALIGN_LEFT_MID, 96, 0);
        lv_obj_align(fc_pop[i], LV_ALIGN_RIGHT_MID, -12, 0);
    }

    ui_set_status(UI_STATUS_LOADING, "Verbinden met Wi-Fi...");
}

/* ================= Bijwerken ================= */

void ui_set_status(ui_status_t status, const char *text)
{
    uint32_t color = status == UI_STATUS_OK      ? C_SUCCESS
                     : status == UI_STATUS_ERROR ? C_DANGER
                                                 : C_ATTENTION;
    lv_obj_set_style_bg_color(status_dot, lv_color_hex(color), 0);
    lv_label_set_text(status_text, text);
}

void ui_update(const weather_t *w)
{
    char buf[64], t1[8], t2[8], date[12];

    /* Datum van de meting (in Nederlandse tijd) achter "Nu" */
    format_date(w->dt, w->tz_offset, date, sizeof(date));
    lv_label_set_text_fmt(now_title, "Nu: %s", date);

    lv_label_set_text_fmt(temp_label, "%d" DEG, round_temp(w->temp));
    lv_label_set_text_fmt(minmax_label, "min %d" DEG "  max %d" DEG,
                          round_temp(w->temp_min), round_temp(w->temp_max));

    capitalize(w->description, buf, sizeof(buf));
    set_pill(cond_pill, cond_pill_text, color_for_condition(w->main), buf);

    lv_label_set_text_fmt(stat_value[0], "%d" DEG, round_temp(w->feels_like));
    lv_label_set_text_fmt(stat_value[1], "%d km/h %s", (int)lroundf(w->wind_kmh),
                          wind_direction(w->wind_deg));
    lv_label_set_text_fmt(stat_value[2], "%d%%  %d hPa", w->humidity, w->pressure);
    format_time(w->sunrise, w->tz_offset, t1, sizeof(t1));
    format_time(w->sunset, w->tz_offset, t2, sizeof(t2));
    lv_label_set_text_fmt(stat_value[3], "%s / %s", t1, t2);

    for (int i = 0; i < FORECAST_COUNT; i++)
    {
        if (i < w->forecast_count)
        {
            const forecast_t *f = &w->forecast[i];
            format_time(f->dt, w->tz_offset, t1, sizeof(t1));
            lv_label_set_text(fc_time[i], t1);
            lv_label_set_text_fmt(fc_temp[i], "%d" DEG, round_temp(f->temp));
            capitalize(f->description, buf, sizeof(buf));
            lv_label_set_text(fc_desc[i], buf);
            lv_obj_set_style_text_color(fc_desc[i], lv_color_hex(color_for_condition(f->main)), 0);
            if (f->pop > 0)
                lv_label_set_text_fmt(fc_pop[i], "%d%%", f->pop);
            else
                lv_label_set_text(fc_pop[i], "");
        }
        else
        {
            lv_label_set_text(fc_time[i], "--:--");
            lv_label_set_text(fc_temp[i], "");
            lv_label_set_text(fc_desc[i], "");
            lv_label_set_text(fc_pop[i], "");
        }
    }

    format_time(w->dt, w->tz_offset, t1, sizeof(t1));
    snprintf(buf, sizeof(buf), "Bijgewerkt %s", t1);
    ui_set_status(UI_STATUS_OK, buf);
}
