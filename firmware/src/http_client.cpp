#include "http_client.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <esp_wifi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include "log.h"

#ifndef DAEMON_PATH
#define DAEMON_PATH "/api/usage"
#endif

#ifndef DAEMON_REFRESH_PATH
#define DAEMON_REFRESH_PATH "/api/refresh"
#endif

#ifndef DAEMON_USE_TLS
#define DAEMON_USE_TLS 0
#endif

static String mac_url;
static String refresh_url;
static String daemon_ip;
static String daemon_port;
static HTTPClient http_client;
static WiFiClientSecure secure_client;
static uint32_t last_poll_ms = 0;
static uint32_t last_successful_poll_ms = 0;
static const uint32_t POLL_INTERVAL_MS = 30000;
static const uint32_t HTTP_TIMEOUT_MS = 8000;
static UsageData last_valid_data = {0, -1, 0, -1, "cached", false, false};

void http_client_init(const char* mac_ip, const char* mac_port) {
    http_client_set_server(mac_ip, mac_port);
}

void http_client_set_server(const char* mac_ip, const char* mac_port) {
    daemon_ip = String(mac_ip);
    daemon_port = String(mac_port);
#if DAEMON_USE_TLS
    mac_url = String("https://") + mac_ip + ":" + mac_port + DAEMON_PATH;
    refresh_url = String("https://") + mac_ip + ":" + mac_port + DAEMON_REFRESH_PATH;
    secure_client.setInsecure();
    secure_client.setTimeout(15);
#else
    mac_url = String("http://") + mac_ip + ":" + mac_port + DAEMON_PATH;
    refresh_url = String("http://") + mac_ip + ":" + mac_port + DAEMON_REFRESH_PATH;
#endif
}

void http_client_tick(void) {
    // Passive - polling happens in fetch_usage
}


bool http_client_fetch_usage(UsageData* out) {
    uint32_t now = millis();

    if (now - last_poll_ms < POLL_INTERVAL_MS) {
        if (last_valid_data.valid) {
            *out = last_valid_data;
            return true;
        }
        return false;
    }

    wifi_ap_record_t ap_info = {};
    if (esp_wifi_sta_get_ap_info(&ap_info) != ESP_OK) {
        return false;
    }

    last_poll_ms = now;
    bool success = false;

    http_client.setConnectTimeout(HTTP_TIMEOUT_MS);
    http_client.setTimeout(HTTP_TIMEOUT_MS);

#if DAEMON_USE_TLS
    if (http_client.begin(secure_client, mac_url)) {
#else
    if (http_client.begin(mac_url)) {
#endif
        int httpCode = http_client.GET();

        if (httpCode == 200) {
            String payload = http_client.getString();
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, payload);

            if (!err) {
                out->session_pct = doc["s"] | 0.0f;
                out->session_reset_mins = doc["sr"] | -1;
                out->weekly_pct = doc["w"] | 0.0f;
                out->weekly_reset_mins = doc["wr"] | -1;
                strlcpy(out->status, doc["st"] | "unknown", sizeof(out->status));
                out->ok = doc["ok"] | false;
                out->valid = true;

                last_valid_data = *out;
                last_successful_poll_ms = now;
                log_format("Updated %.0f%% / %.0f%%", out->session_pct, out->weekly_pct);
                success = true;
            } else {
                log_add("JSON parse failed");
            }
        } else {
            log_format("HTTP err %d", httpCode);
        }

        http_client.end();
    } else {
        log_add("HTTP begin failed");
    }

    return success;
}

bool http_client_is_connected(void) {
    wifi_ap_record_t ap_info = {};
    return esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK;
}

void http_client_request_refresh(void) {
    if (!http_client_is_connected()) {
        return;
    }

    http_client.setConnectTimeout(HTTP_TIMEOUT_MS);
    http_client.setTimeout(HTTP_TIMEOUT_MS);

#if DAEMON_USE_TLS
    if (http_client.begin(secure_client, refresh_url)) {
#else
    if (http_client.begin(refresh_url)) {
#endif
        http_client.addHeader("Content-Type", "application/json");
        int httpCode = http_client.POST("{}");
        if (httpCode == 200) {
            log_add("Refresh requested");
        } else {
            log_format("Refresh HTTP %d", httpCode);
        }
        http_client.end();
    }
}
