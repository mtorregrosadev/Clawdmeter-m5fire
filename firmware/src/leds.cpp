#include "leds.h"
#include <Adafruit_NeoPixel.h>

#define LED_PIN 15
#define NUM_LEDS 10

static Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
static led_state_t current_state = LED_OFF;
static uint32_t blink_start_ms = 0;
static const uint32_t BLINK_DURATION_MS = 500;
static const uint32_t LED_ORANGE_TIMEOUT_MS = 2000;

void led_init(void) {
    strip.begin();
    strip.show();
    led_set_color(0, 0, 0);
}

void led_set_color(uint8_t r, uint8_t g, uint8_t b) {
    for (int i = 0; i < NUM_LEDS; i++) {
        strip.setPixelColor(i, strip.Color(r, g, b));
    }
    strip.show();
}

void led_set(led_state_t state) {
    current_state = state;
    blink_start_ms = millis();

    switch (state) {
        case LED_ORANGE:
            led_set_color(255, 165, 0);  // Orange
            break;
        case LED_RED_BLINK:
            led_set_color(255, 0, 0);    // Red for first flash
            break;
        case LED_BLUE_BLINK:
            led_set_color(0, 0, 255);    // Blue for first flash
            break;
        case LED_OFF:
        default:
            led_set_color(0, 0, 0);      // Off
            break;
    }
}

void led_tick(void) {
    uint32_t elapsed = millis() - blink_start_ms;

    if (current_state == LED_ORANGE) {
        if (elapsed > LED_ORANGE_TIMEOUT_MS) {
            led_set(LED_OFF);
        }
    } else if (current_state == LED_RED_BLINK) {
        uint32_t cycle = elapsed % BLINK_DURATION_MS;
        if (cycle < BLINK_DURATION_MS / 2) {
            led_set_color(255, 0, 0);
        } else {
            led_set_color(0, 0, 0);
        }
    } else if (current_state == LED_BLUE_BLINK) {
        uint32_t cycle = elapsed % BLINK_DURATION_MS;
        if (cycle < BLINK_DURATION_MS / 2) {
            led_set_color(0, 0, 255);
        } else {
            led_set_color(0, 0, 0);
        }
    }
}
