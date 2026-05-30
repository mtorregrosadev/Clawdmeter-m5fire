#include <Arduino.h>
#include <M5Unified.h>
#include <ArduinoJson.h>
#include "config.h"
#include "data.h"
#include "ui.h"
#include "power.h"
#include "imu.h"
#include "splash.h"
#include "usage_rate.h"
#include "leds.h"
#include "wifi.h"
#include "http_client.h"
#include "log.h"

static UsageData usage = {};
static uint32_t last_poll_ms = 0;
static uint32_t force_poll_at_ms = 0;  // Force poll at specific time
static const uint32_t POLL_INTERVAL_MS = 30000; // 30 seconds
static const uint32_t DAEMON_SCRAPE_DELAY_MS = 2500; // Wait for daemon to scrape

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

static void poll_wifi_usage() {
    uint32_t now = millis();
    bool force_poll = (force_poll_at_ms > 0 && now >= force_poll_at_ms);
    bool regular_poll = (now - last_poll_ms >= POLL_INTERVAL_MS);

    if (!force_poll && !regular_poll) return;

    if (!http_client_is_connected()) {
        return;
    }

    last_poll_ms = now;
    force_poll_at_ms = 0;

    UsageData incoming = usage;
    if (http_client_fetch_usage(&incoming)) {
        usage = incoming;
        usage_rate_sample(usage.session_pct);
        ui_update(&usage);
        led_set(LED_ORANGE);
        char log_msg[64];
        snprintf(log_msg, sizeof(log_msg), "Updated: %d%% / %d%%",
            (int)incoming.session_pct, (int)incoming.weekly_pct);
        log_add(log_msg);
    } else {
        led_set(LED_RED_BLINK);
        log_add("API error: timeout/connection");
    }
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);

    log_init();
    log_add("System booting...");

    power_init();
    imu_init();

    splash_init();
    ui_init();
    led_init();

    wifi_init();
    http_client_init(DAEMON_IP, DAEMON_PORT);

    log_add("WiFi init complete");
    ui_update_battery(power_battery_pct(), power_is_charging());
    ui_show_screen(SCREEN_SPLASH);
    seed_demo_data();
    Serial.println("{\"ready\":true}");
}

static void handle_serial_screenshot() {
    if (!Serial.available()) return;
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd != "screenshot") return;

    const int W = M5.Display.width();
    const int H = M5.Display.height();
    const int STRIP_H = 8;
    const size_t strip_bytes = (size_t)W * STRIP_H * 3;
    const size_t total = (size_t)W * H * 3;

    uint8_t* buf = (uint8_t*)malloc(strip_bytes);
    if (!buf) {
        Serial.println("SCREENSHOT_ERR");
        return;
    }

    Serial.printf("SCREENSHOT_START %d %d %u rgb24\n", W, H, (unsigned)total);
    Serial.flush();

    for (int y = 0; y < H; y += STRIP_H) {
        int rows = (y + STRIP_H > H) ? (H - y) : STRIP_H;
        M5.Display.readRectRGB(0, y, W, rows, buf);
        Serial.write(buf, (size_t)W * rows * 3);
        Serial.flush();
    }

    Serial.println("SCREENSHOT_END");
    free(buf);
}

void loop() {
    M5.update();
    ui_tick_anim();
    ui_tick();
    power_tick();
    imu_tick();
    splash_tick();
    led_tick();
    wifi_tick();
    http_client_tick();
    poll_wifi_usage();
    handle_serial_screenshot();

    if (M5.BtnA.wasPressed()) {
        ui_toggle_splash();
    }
    if (M5.BtnA.pressedFor(2000)) {
        if (ui_get_current_screen() == SCREEN_WIFI) {
            wifi_reconfigure();
            log_add("WiFi reconfiguration started");
        }
    }
    if (M5.BtnB.wasPressed()) {
        if (ui_get_current_screen() == SCREEN_SPLASH) splash_next();
        else ui_cycle_screen();
    }
    if (M5.BtnC.wasPressed()) {
        if (splash_is_active()) splash_pick_for_current_rate();
        else if (ui_get_current_screen() == SCREEN_USAGE) {
            // Request immediate refresh from daemon and schedule forced poll after daemon scrapes
            http_client_request_refresh();
            force_poll_at_ms = millis() + DAEMON_SCRAPE_DELAY_MS;
            led_set(LED_ORANGE);
            log_add("Manual refresh requested");
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

    static uint32_t last_wifi_update = 0;
    uint32_t now = millis();
    if (now - last_wifi_update >= 5000) {
        last_wifi_update = now;
        ui_update_wifi_status(wifi_get_ssid(), wifi_get_signal_strength(), wifi_get_ip(), wifi_is_connected());
    }

    delay(5);
}
