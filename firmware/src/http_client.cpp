#include "http_client.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <esp_wifi.h>

static String mac_url;
static HTTPClient http_client;
static uint32_t last_poll_ms = 0;
static uint32_t last_successful_poll_ms = 0;
static const uint32_t POLL_INTERVAL_MS = 30000;
static const uint32_t HTTP_TIMEOUT_MS = 3000;
static UsageData last_valid_data = {0, -1, 0, -1, "cached", false, false};

void http_client_init(const char* mac_ip, const char* mac_port) {
    http_client_set_server(mac_ip, mac_port);
}

void http_client_set_server(const char* mac_ip, const char* mac_port) {
    mac_url = String("http://") + mac_ip + ":" + mac_port + "/api/usage";
}

void http_client_tick(void) {
    // Passive - polling happens in fetch_usage
}

bool http_client_fetch_usage(UsageData* out) {
    uint32_t now = millis();

    // Don't poll too frequently
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

    if (http_client.begin(mac_url)) {
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
                success = true;
            }
        }

        http_client.end();
    }

    return success;
}

bool http_client_is_connected(void) {
    wifi_ap_record_t ap_info = {};
    return esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK;
}

void http_client_request_refresh(void) {
    // Send POST to /api/refresh to trigger immediate scrape on daemon
    if (!http_client_is_connected()) {
        Serial.println("DEBUG: Can't refresh, WiFi not connected");
        return;
    }

    // Construct refresh URL from mac_url: http://192.168.1.105:80/api/usage -> http://192.168.1.105:80/api/refresh
    String refresh_url = mac_url.substring(0, mac_url.lastIndexOf('/')) + "/refresh";

    Serial.printf("DEBUG: Requesting daemon refresh at %s\n", refresh_url.c_str());

    http_client.setConnectTimeout(HTTP_TIMEOUT_MS);
    http_client.setTimeout(HTTP_TIMEOUT_MS);

    if (http_client.begin(refresh_url)) {
        http_client.addHeader("Content-Type", "application/json");
        int httpCode = http_client.POST("{}");

        if (httpCode == 200) {
            Serial.println("DEBUG: Refresh requested successfully");
        } else {
            Serial.printf("DEBUG: Refresh request failed (HTTP %d)\n", httpCode);
        }

        http_client.end();
    }
}
