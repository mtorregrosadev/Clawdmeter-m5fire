#pragma once

#include <M5Unified.h>

enum led_state_t {
    LED_OFF,
    LED_ORANGE,      // Update success
    LED_RED_BLINK,   // Error
    LED_BLUE_BLINK,  // WiFi reconfiguration mode
};

void led_init(void);
void led_set(led_state_t state);
void led_tick(void);
void led_set_color(uint8_t r, uint8_t g, uint8_t b);
