#include "splash.h"
#include "clawd_assets.h"
#include "theme.h"
#include <M5Unified.h>

static bool active = false;
static uint16_t frame = 0;
static uint32_t last_ms = 0;

static uint16_t color565(uint32_t hex) {
    uint8_t r = (hex >> 16) & 0xFF;
    uint8_t g = (hex >> 8) & 0xFF;
    uint8_t b = hex & 0xFF;
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

static void draw_frame(uint16_t frame_index) {
    const auto& anim = kClawdSplashAnimation;
    const uint32_t frame_size = (uint32_t)anim.width * anim.height;
    const uint16_t* pixels = anim.frames + (frame_index % anim.frame_count) * frame_size;
    const int16_t x = (M5.Display.width() - anim.width) / 2;
    const int16_t y = 24;

    M5.Display.fillScreen(color565(THEME_BG));
    M5.Display.setTextColor(color565(THEME_TEXT), color565(THEME_BG));
    M5.Display.setTextDatum(top_center);
    M5.Display.drawString("Clawdmeter Fire", M5.Display.width() / 2, 10);
    M5.Display.pushImage(x, y, anim.width, anim.height, pixels);
    M5.Display.setTextColor(color565(THEME_ACCENT), color565(THEME_BG));
    M5.Display.setTextDatum(bottom_center);
    M5.Display.drawString(anim.name, M5.Display.width() / 2, 232);
}

void splash_init(void) {
    active = false;
    frame = 0;
    last_ms = 0;
}

void splash_tick(void) {
    if (!active) return;
    uint32_t now = millis();
    if (now - last_ms < kClawdSplashAnimation.frame_delay_ms) return;
    last_ms = now;
    frame = (frame + 1) % kClawdSplashAnimation.frame_count;
    draw_frame(frame);
}

void splash_next(void) {
    frame = (frame + 1) % kClawdSplashAnimation.frame_count;
    draw_frame(frame);
}

void splash_show(void) {
    active = true;
    last_ms = millis();
    draw_frame(frame);
}

void splash_hide(void) {
    active = false;
}

void splash_pick_for_current_rate(void) {
    draw_frame(frame);
}

bool splash_is_active(void) {
    return active;
}

void splash_render_static(void) {
    draw_frame(frame);
}
