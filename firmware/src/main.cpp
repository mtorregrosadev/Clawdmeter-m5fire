#include <Arduino.h>
#include <M5Unified.h>
#include <ArduinoJson.h>
#include "data.h"
#include "ui.h"
#include "power.h"
#include "imu.h"
#include "splash.h"
#include "usage_rate.h"
#include "leds.h"

static UsageData usage = {};

static bool parse_json(const char* json, UsageData* out) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) return false;
    out->session_pct = doc["s"] | 0.0f;
    out->session_reset_mins = doc["sr"] | -1;
    out->weekly_pct = doc["w"] | 0.0f;
    out->weekly_reset_mins = doc["wr"] | -1;
    strlcpy(out->status, doc["st"] | "unknown", sizeof(out->status));
    out->ok = doc["ok"] | false;
    out->valid = true;
    return true;
}

static void seed_demo_data(void) {
    // Demo: 45% used, 2h 45m remaining (165 min), 28% used, 5d 2h remaining (7320 min)
    const char* demo = "{\"s\":45,\"sr\":165,\"w\":28,\"wr\":7320,\"st\":\"demo\",\"ok\":true}";
    if (parse_json(demo, &usage)) {
        usage_rate_sample(usage.session_pct);
        ui_update(&usage);
    }
}

static void poll_serial_usage() {
    static char line[256];
    static size_t pos = 0;

    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\r') continue;
        if (c == '\n') {
            line[pos] = '\0';
            if (pos > 0) {
                UsageData incoming = usage;
                if (parse_json(line, &incoming)) {
                    usage = incoming;
                    usage_rate_sample(usage.session_pct);
                    ui_update(&usage);
                    led_set(LED_ORANGE);
                    Serial.println("{\"ack\":true}");
                } else {
                    led_set(LED_RED_BLINK);
                }
            }
            pos = 0;
        } else if (pos < sizeof(line) - 1) {
            line[pos++] = c;
        }
    }
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);

    power_init();
    imu_init();

    splash_init();
    ui_init();
    led_init();
    ui_update_ble_status(FIRE_LINK_UNUSED, nullptr, nullptr);
    ui_update_battery(power_battery_pct(), power_is_charging());
    ui_show_screen(SCREEN_SPLASH);
    seed_demo_data();
    Serial.println("{\"ready\":true}");
}

void loop() {
    M5.update();
    ui_tick_anim();
    power_tick();
    imu_tick();
    splash_tick();
    led_tick();
    poll_serial_usage();

    if (M5.BtnA.wasPressed()) {
        ui_toggle_splash();
    }
    if (M5.BtnB.wasPressed()) {
        if (ui_get_current_screen() == SCREEN_SPLASH) splash_next();
        else ui_cycle_screen();
    }
    if (M5.BtnC.wasPressed()) {
        if (splash_is_active()) splash_pick_for_current_rate();
        else if (ui_get_current_screen() == SCREEN_GALLERY) ui_cycle_gallery_visual();
        else if (ui_get_current_screen() == SCREEN_USAGE) {
            // Request immediate scraper refresh
            Serial.println("{\"cmd\":\"refresh\"}");
            led_set(LED_ORANGE);
        }
    }

    static int last_pct = -2;
    static bool last_charging = false;
    int pct = power_battery_pct();
    bool charging = power_is_charging();
    if (pct != last_pct || charging != last_charging) {
        last_pct = pct;
        last_charging = charging;
        ui_update_battery(pct, charging);
    }

    delay(5);
}
