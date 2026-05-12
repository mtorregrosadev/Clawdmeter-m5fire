#include "ui.h"
#include "splash.h"
#include "theme.h"
#include <M5Unified.h>

static screen_t current_screen = SCREEN_USAGE;
static screen_t prev_non_splash_screen = SCREEN_USAGE;
static UsageData current_data = {};
static int battery_percent = -1;
static bool battery_charging = false;
static uint32_t anim_last_ms = 0;
static uint8_t anim_msg_idx = 0;

static const char* const anim_messages[] = {
    "Accomplishing", "Elucidating", "Perusing", "Thinking", "Working", "Processing"
};
#define ANIM_MSG_COUNT (sizeof(anim_messages) / sizeof(anim_messages[0]))

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
    if (mins < 0) snprintf(buf, len, "---");
    else if (mins < 60) snprintf(buf, len, "Resets in %dm", mins);
    else if (mins < 1440) snprintf(buf, len, "Resets in %dh %dm", mins / 60, mins % 60);
    else snprintf(buf, len, "Resets in %dd %dh", mins / 1440, (mins % 1440) / 60);
}

static void draw_bar(int x, int y, int w, int h, int pct, uint16_t fill) {
    M5.Display.fillRoundRect(x, y, w, h, 6, color565(THEME_BAR_BG));
    int fw = (w * (pct < 0 ? 0 : (pct > 100 ? 100 : pct))) / 100;
    if (fw > 0) M5.Display.fillRoundRect(x, y, fw, h, 6, fill);
}

static void draw_header(const char* title) {
    M5.Display.setTextColor(color565(THEME_TEXT), color565(THEME_BG));
    M5.Display.setTextDatum(top_center);
    M5.Display.drawString(title, 160, 12);
}

static void draw_battery(void) {
    int x = 280, y = 12, w = 24, h = 12;
    uint16_t border = color565(THEME_TEXT);
    uint16_t fillc = battery_charging ? color565(THEME_GREEN) : color565(THEME_TEXT);
    M5.Display.drawRoundRect(x, y, w, h, 2, border);
    M5.Display.fillRect(x + w, y + 3, 2, 6, border);
    int fillw = 0;
    if (battery_charging) fillw = w - 4;
    else if (battery_percent > 75) fillw = w - 4;
    else if (battery_percent > 35) fillw = (w - 4) * 2 / 3;
    else if (battery_percent > 10) fillw = (w - 4) / 3;
    if (fillw > 0) M5.Display.fillRect(x + 2, y + 2, fillw, h - 4, fillc);
}

static void draw_usage_screen(void) {
    M5.Display.fillScreen(color565(THEME_BG));
    draw_header("Usage");
    draw_battery();

    M5.Display.setTextDatum(top_left);
    M5.Display.setTextColor(color565(THEME_TEXT), color565(THEME_PANEL));

    M5.Display.fillRoundRect(10, 52, 300, 72, 8, color565(THEME_PANEL));
    M5.Display.drawString("Current", 220, 60);
    M5.Display.drawString(current_data.valid ? String((int)(current_data.session_pct + 0.5f)) + "%" : "---%", 24, 60);
    draw_bar(24, 88, 268, 12, current_data.valid ? (int)(current_data.session_pct + 0.5f) : 0, pct_color(current_data.session_pct));
    char buf[32];
    format_reset_time(current_data.session_reset_mins, buf, sizeof(buf));
    M5.Display.setTextColor(color565(THEME_DIM), color565(THEME_PANEL));
    M5.Display.setTextDatum(top_center);
    M5.Display.drawString(buf, 160, 104);

    M5.Display.setTextDatum(top_left);
    M5.Display.setTextColor(color565(THEME_TEXT), color565(THEME_PANEL));
    M5.Display.fillRoundRect(10, 134, 300, 72, 8, color565(THEME_PANEL));
    M5.Display.drawString("Weekly", 228, 142);
    M5.Display.drawString(current_data.valid ? String((int)(current_data.weekly_pct + 0.5f)) + "%" : "---%", 24, 142);
    draw_bar(24, 170, 268, 12, current_data.valid ? (int)(current_data.weekly_pct + 0.5f) : 0, pct_color(current_data.weekly_pct));
    format_reset_time(current_data.weekly_reset_mins, buf, sizeof(buf));
    M5.Display.setTextColor(color565(THEME_DIM), color565(THEME_PANEL));
    M5.Display.setTextDatum(top_center);
    M5.Display.drawString(buf, 160, 186);

    M5.Display.setTextColor(color565(THEME_ACCENT), color565(THEME_BG));
    M5.Display.setTextDatum(bottom_center);
    M5.Display.drawString(anim_messages[anim_msg_idx], 160, 236);
}

static void draw_system_screen(void) {
    M5.Display.fillScreen(color565(THEME_BG));
    draw_header("Fire status");
    draw_battery();

    M5.Display.fillRoundRect(10, 52, 300, 120, 8, color565(THEME_PANEL));
    M5.Display.setTextDatum(top_left);
    M5.Display.setTextColor(color565(THEME_TEXT), color565(THEME_PANEL));
    M5.Display.drawString("Standalone dashboard", 24, 64);
    M5.Display.setTextColor(color565(THEME_DIM), color565(THEME_PANEL));
    M5.Display.drawString("Device: M5Stack Fire v2.7", 24, 90);
    M5.Display.drawString("A splash  B screens  C refresh demo", 24, 112);
    M5.Display.drawString("Ported locally for M5Stack Fire", 24, 134);
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
        case SCREEN_BLUETOOTH:
            splash_hide();
            draw_system_screen();
            break;
        default:
            break;
    }
}

void ui_init(void) {
    M5.Display.setRotation(1);
    M5.Display.setTextSize(1);
    M5.Display.setFont(&fonts::Font2);
    redraw();
}

void ui_update(const UsageData* data) {
    current_data = *data;
    if (current_screen == SCREEN_USAGE) redraw();
}

void ui_tick_anim(void) {
    uint32_t now = millis();
    if (now - anim_last_ms >= 4000) {
        anim_last_ms = now;
        anim_msg_idx = (anim_msg_idx + 1) % ANIM_MSG_COUNT;
        if (current_screen == SCREEN_USAGE) redraw();
    }
}

void ui_show_screen(screen_t screen) {
    if (screen != SCREEN_SPLASH) prev_non_splash_screen = screen;
    current_screen = screen;
    redraw();
}

void ui_cycle_screen(void) {
    screen_t next = (current_screen == SCREEN_USAGE) ? SCREEN_BLUETOOTH : SCREEN_USAGE;
    ui_show_screen(next);
}

void ui_toggle_splash(void) {
    if (current_screen == SCREEN_SPLASH) ui_show_screen(prev_non_splash_screen);
    else ui_show_screen(SCREEN_SPLASH);
}

screen_t ui_get_current_screen(void) {
    return current_screen;
}

void ui_update_ble_status(fire_link_state_t state, const char* name, const char* mac) {
    (void)state;
    (void)name;
    (void)mac;
    if (current_screen == SCREEN_BLUETOOTH) redraw();
}

void ui_update_battery(int percent, bool charging) {
    battery_percent = percent;
    battery_charging = charging;
    if (current_screen != SCREEN_SPLASH) redraw();
}
