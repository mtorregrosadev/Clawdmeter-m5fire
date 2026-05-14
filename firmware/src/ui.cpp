#include "ui.h"
#include "clawd_assets.h"
#include "splash.h"
#include "theme.h"
#include "logo.h"
#include "log.h"
#include <M5Unified.h>
#include <math.h>

static screen_t current_screen = SCREEN_USAGE;
static screen_t prev_non_splash_screen = SCREEN_USAGE;
static UsageData current_data = {};
static int battery_percent = -1;
static bool battery_charging = false;
static const char* wifi_ssid = "N/A";
static int wifi_signal = 0;
static const char* wifi_ip = "0.0.0.0";
static bool wifi_connected = false;

static const char* const kFooterText = "* mtorregrosadev :p";

static uint16_t color565(uint32_t hex) {
    uint8_t r = (hex >> 16) & 0xFF;
    uint8_t g = (hex >> 8) & 0xFF;
    uint8_t b = hex & 0xFF;
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

static uint16_t pct_color(float pct) {
    if (pct >= 80.0f) return color565(THEME_RED);
    if (pct >= 50.0f) return color565(THEME_AMBER);
    return color565(THEME_GREEN);
}

static void format_reset_time(int mins, char* buf, size_t len) {
    if (mins < 0) snprintf(buf, len, "Resets in --");
    else if (mins < 60) snprintf(buf, len, "Resets in %dm", mins);
    else if (mins < 1440) snprintf(buf, len, "Resets in %dh %dm", mins / 60, mins % 60);
    else snprintf(buf, len, "Resets in %dd %dh", mins / 1440, (mins % 1440) / 60);
}

static void set_font_title() { M5.Display.setFont(&fonts::FreeSerifBold18pt7b); }
static void set_font_body() { M5.Display.setFont(&fonts::Font2); }
static void set_font_big() { M5.Display.setFont(&fonts::Font4); }
static void set_font_footer() { M5.Display.setFont(&fonts::Font2); }

static void draw_bar(int x, int y, int w, int h, int pct, uint16_t fill) {
    M5.Display.fillRoundRect(x, y, w, h, 7, color565(THEME_BAR_BG));
    int fw = (w * (pct < 0 ? 0 : (pct > 100 ? 100 : pct))) / 100;
    if (fw > 0) M5.Display.fillRoundRect(x, y, fw, h, 7, fill);
}

static void draw_logo() {
    M5.Display.pushImage(14, 10, 32, 32, kClawdLogo_pixels);
}

static void draw_battery(void) {
    int x = 274, y = 18, w = 24, h = 12;
    uint16_t border = color565(THEME_TEXT);
    uint16_t fillc = battery_charging ? color565(THEME_GREEN) : color565(THEME_TEXT);
    M5.Display.drawRoundRect(x, y, w, h, 3, border);
    M5.Display.fillRect(x + w, y + 3, 2, 6, border);
    int fillw = 0;
    if (battery_charging) fillw = w - 4;
    else if (battery_percent > 75) fillw = w - 4;
    else if (battery_percent > 35) fillw = (w - 4) * 2 / 3;
    else if (battery_percent > 10) fillw = (w - 4) / 3;
    if (fillw > 0) M5.Display.fillRoundRect(x + 2, y + 2, fillw, h - 4, 2, fillc);
}

static void draw_header(const char* title) {
    draw_logo();
    set_font_title();
    M5.Display.setTextColor(color565(THEME_TEXT), color565(THEME_BG));
    M5.Display.setTextDatum(top_center);
    M5.Display.drawString(title, 160, 10);
    draw_battery();
}

static void draw_pill(const char* text, int x, int y, int w) {
    M5.Display.fillRoundRect(x, y, w, 22, 11, color565(0x2d2d2d));
    set_font_body();
    M5.Display.setTextColor(color565(THEME_TEXT), color565(0x2d2d2d));
    M5.Display.setTextDatum(middle_center);
    M5.Display.drawString(text, x + w / 2, y + 11);
}

static void draw_usage_card(int y, const char* label, float pctf, int reset_mins, int label_w) {
    int x = 12, w = 296, h = 78;
    M5.Display.fillRoundRect(x, y, w, h, 8, color565(THEME_PANEL));

    char pct[16];
    snprintf(pct, sizeof(pct), "%d%%", (int)lroundf(pctf));
    set_font_big();
    M5.Display.setTextColor(color565(THEME_TEXT), color565(THEME_PANEL));
    M5.Display.setTextDatum(top_left);
    M5.Display.drawString(pct, x + 14, y + 4);

    draw_pill(label, x + w - label_w - 14, y + 10, label_w);

    draw_bar(x + 12, y + 36, w - 24, 12, (int)lroundf(pctf), pct_color(pctf));

    char buf[48];
    format_reset_time(reset_mins, buf, sizeof(buf));
    set_font_body();
    M5.Display.setTextColor(color565(THEME_DIM), color565(THEME_PANEL));
    M5.Display.setTextDatum(top_left);
    M5.Display.drawString(buf, x + 12, y + 50);
}

static void draw_usage_screen(void) {
    M5.Display.fillScreen(color565(THEME_BG));
    draw_header("Usage");
    draw_usage_card(62, "Current", current_data.valid ? current_data.session_pct : 0, current_data.session_reset_mins, 92);
    draw_usage_card(146, "Weekly", current_data.valid ? current_data.weekly_pct : 0, current_data.weekly_reset_mins, 82);

    set_font_footer();
    M5.Display.setTextColor(color565(THEME_ACCENT), color565(THEME_BG));
    M5.Display.setTextDatum(bottom_center);
    M5.Display.drawString(kFooterText, 160, 233);
}

static void draw_wifi_screen(void) {
    M5.Display.fillScreen(color565(THEME_BG));
    draw_header("WiFi Status");
    M5.Display.fillRoundRect(12, 72, 296, 132, 8, color565(THEME_PANEL));
    set_font_body();
    M5.Display.setTextDatum(top_left);
    M5.Display.setTextColor(color565(THEME_TEXT), color565(THEME_PANEL));

    char status_text[32];
    snprintf(status_text, sizeof(status_text), wifi_connected ? "Connected" : "Disconnected");
    M5.Display.drawString(status_text, 24, 88);

    M5.Display.setTextColor(color565(THEME_DIM), color565(THEME_PANEL));
    M5.Display.drawString(wifi_ssid, 24, 110);

    char ip_text[24];
    snprintf(ip_text, sizeof(ip_text), "IP: %s", wifi_ip);
    M5.Display.drawString(ip_text, 24, 130);

    char signal_text[24];
    snprintf(signal_text, sizeof(signal_text), "Signal: %d%%", wifi_signal);
    M5.Display.drawString(signal_text, 24, 150);

    M5.Display.drawString("A splash", 24, 182);
    M5.Display.drawString("B screen", 24, 202);
}

static void draw_log_screen(void) {
    M5.Display.fillScreen(color565(THEME_BG));
    draw_header("System Log");

    set_font_body();
    M5.Display.setTextColor(color565(THEME_TEXT), color565(THEME_BG));
    M5.Display.setTextDatum(top_left);

    int log_count = log_get_count();
    int start_line = log_count > 10 ? log_count - 10 : 0;
    int y = 50;

    for (int i = start_line; i < log_count && y < 220; i++) {
        const char* line = log_get_line(i);
        if (line && *line) {
            M5.Display.drawString(line, 12, y);
            y += 16;
        }
    }
}

static void redraw(void) {
    switch (current_screen) {
        case SCREEN_SPLASH:
            splash_show();
            break;
        case SCREEN_USAGE:
            splash_hide();
            draw_usage_screen();
            break;
        case SCREEN_LOG:
            splash_hide();
            draw_log_screen();
            break;
        case SCREEN_WIFI:
            splash_hide();
            draw_wifi_screen();
            break;
        default:
            break;
    }
}

void ui_init(void) {
    M5.Display.setRotation(1);
    M5.Display.setTextSize(1);
    redraw();
}

void ui_update(const UsageData* data) {
    current_data = *data;
    if (current_screen == SCREEN_USAGE) redraw();
}

void ui_tick_anim(void) {
    // Animation tick - not needed for log screen
}

void ui_show_screen(screen_t screen) {
    if (screen != SCREEN_SPLASH) prev_non_splash_screen = screen;
    current_screen = screen;
    redraw();
}

void ui_cycle_screen(void) {
    screen_t next = SCREEN_USAGE;
    switch (current_screen) {
        case SCREEN_USAGE:
            next = SCREEN_LOG;
            break;
        case SCREEN_LOG:
            next = SCREEN_WIFI;
            break;
        case SCREEN_WIFI:
        default:
            next = SCREEN_USAGE;
            break;
    }
    ui_show_screen(next);
}

void ui_toggle_splash(void) {
    if (current_screen == SCREEN_SPLASH) ui_show_screen(prev_non_splash_screen);
    else ui_show_screen(SCREEN_SPLASH);
}

screen_t ui_get_current_screen(void) {
    return current_screen;
}

void ui_update_wifi_status(const char* ssid, int signal_strength, const char* ip, bool connected) {
    wifi_ssid = ssid ? ssid : "N/A";
    wifi_signal = signal_strength;
    wifi_ip = ip ? ip : "0.0.0.0";
    wifi_connected = connected;
    if (current_screen == SCREEN_WIFI) redraw();
}

void ui_update_battery(int percent, bool charging) {
    battery_percent = percent;
    battery_charging = charging;
    if (current_screen != SCREEN_SPLASH) redraw();
}
