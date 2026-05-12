#include "splash.h"
#include "theme.h"
#include <M5Unified.h>

static bool active = false;
static uint8_t frame = 0;
static uint32_t last_ms = 0;

static uint16_t color565(uint32_t hex) {
    uint8_t r = (hex >> 16) & 0xFF;
    uint8_t g = (hex >> 8) & 0xFF;
    uint8_t b = hex & 0xFF;
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

static void draw_clawd(uint8_t f) {
    M5.Display.fillScreen(color565(THEME_BG));

    uint16_t body = color565(0xD97757);
    uint16_t dark = color565(0x8C4F3D);
    uint16_t eye = color565(THEME_BG);
    uint16_t light = color565(0xF2C4A8);

    int bob = (f % 2) ? 2 : 0;

    M5.Display.fillRoundRect(112, 78 + bob, 96, 82, 18, body);
    M5.Display.fillTriangle(122, 90 + bob, 138, 62 + bob, 150, 92 + bob, body);
    M5.Display.fillTriangle(198, 90 + bob, 182, 62 + bob, 170, 92 + bob, body);
    M5.Display.fillRoundRect(124, 145 + bob, 18, 24, 6, dark);
    M5.Display.fillRoundRect(178, 145 + bob, 18, 24, 6, dark);
    M5.Display.fillCircle(142, 110 + bob, 8, eye);
    M5.Display.fillCircle(178, 110 + bob, 8, eye);
    M5.Display.fillCircle(145, 107 + bob, 2, light);
    M5.Display.fillCircle(181, 107 + bob, 2, light);

    if (f % 3 == 0) {
        M5.Display.drawLine(146, 135 + bob, 160, 142 + bob, eye);
        M5.Display.drawLine(160, 142 + bob, 174, 135 + bob, eye);
    } else if (f % 3 == 1) {
        M5.Display.drawLine(146, 138 + bob, 174, 138 + bob, eye);
    } else {
        M5.Display.drawLine(146, 142 + bob, 160, 135 + bob, eye);
        M5.Display.drawLine(160, 135 + bob, 174, 142 + bob, eye);
    }

    M5.Display.fillCircle(104, 102 + bob, 10, dark);
    M5.Display.fillCircle(216, 102 + bob, 10, dark);

    M5.Display.setTextColor(color565(THEME_TEXT), color565(THEME_BG));
    M5.Display.setTextDatum(bottom_center);
    M5.Display.drawString("Clawdmeter Fire", 160, 232);
}

void splash_init(void) {
    active = false;
}

void splash_tick(void) {
    if (!active) return;
    uint32_t now = millis();
    if (now - last_ms >= 900) {
        last_ms = now;
        frame = (frame + 1) % 6;
        draw_clawd(frame);
    }
}

void splash_next(void) {
    frame = (frame + 1) % 6;
    draw_clawd(frame);
}

void splash_show(void) {
    active = true;
    last_ms = millis();
    draw_clawd(frame);
}

void splash_hide(void) {
    active = false;
}

void splash_pick_for_current_rate(void) {
    draw_clawd(frame);
}

bool splash_is_active(void) {
    return active;
}
