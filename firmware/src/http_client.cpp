#include "http_client.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <esp_wifi.h>
#include <lwip/dns.h>
#include <lwip/netdb.h>
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
static const uint32_t HTTP_TIMEOUT_MS = 3000;
static UsageData last_valid_data = {0, -1, 0, -1, "cached", false, false};

static void debug_network_status(void) {
    Serial.println("\n=== WIFI STATUS ===");
    Serial.print("SSID: ");
    wifi_ap_record_t ap_info = {};
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        Serial.println((const char*)ap_info.ssid);
    } else {
        Serial.println("NOT CONNECTED");
        return;
    }

    Serial.print("Local IP: ");
    esp_netif_ip_info_t ip_info = {};
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif) {
        esp_netif_get_ip_info(netif, &ip_info);
        Serial.printf("%d.%d.%d.%d\n",
            (ip_info.ip.addr >> 0) & 0xFF,
            (ip_info.ip.addr >> 8) & 0xFF,
            (ip_info.ip.addr >> 16) & 0xFF,
            (ip_info.ip.addr >> 24) & 0xFF);

        Serial.print("Gateway: ");
        Serial.printf("%d.%d.%d.%d\n",
            (ip_info.gw.addr >> 0) & 0xFF,
            (ip_info.gw.addr >> 8) & 0xFF,
            (ip_info.gw.addr >> 16) & 0xFF,
            (ip_info.gw.addr >> 24) & 0xFF);

        Serial.print("Subnet: ");
        Serial.printf("%d.%d.%d.%d\n",
            (ip_info.netmask.addr >> 0) & 0xFF,
            (ip_info.netmask.addr >> 8) & 0xFF,
            (ip_info.netmask.addr >> 16) & 0xFF,
            (ip_info.netmask.addr >> 24) & 0xFF);
    }

    Serial.print("RSSI: ");
    Serial.println(ap_info.rssi);

    Serial.println("\n=== TARGET ===");
    Serial.print("Host: ");
    Serial.println(daemon_ip);
    Serial.print("Port: ");
    Serial.println(daemon_port);

    Serial.println("\n=== DNS RESOLUTION ===");
    int port = atoi(daemon_port.c_str());
    struct hostent* he = gethostbyname(daemon_ip.c_str());
    Serial.print("DNS resolve: ");
    if (he && he->h_addr_list[0]) {
        struct in_addr* addr = (struct in_addr*)he->h_addr_list[0];
        Serial.println(inet_ntoa(*addr));
    } else {
        Serial.println("FAIL - cannot resolve hostname");
        return;
    }

#if DAEMON_USE_TLS
    Serial.println("\n=== TLS/HTTPS CONNECT TEST ===");
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(15);

    Serial.print("Attempting TLS connect to ");
    Serial.print(daemon_ip);
    Serial.print(":");
    Serial.println(port);
#else
    Serial.println("\n=== TCP/HTTP CONNECT TEST ===");
    WiFiClient client;

    Serial.print("Attempting TCP connect to ");
    Serial.print(daemon_ip);
    Serial.print(":");
    Serial.println(port);
#endif

    bool connected = client.connect(daemon_ip.c_str(), port);
    Serial.print("connect() result: ");
    Serial.println(connected ? "OK" : "FAIL");

    if (!connected) {
#if DAEMON_USE_TLS
        Serial.println("ERROR: Could not establish TLS connection");
        log_add("TLS connect failed");
#else
        Serial.println("ERROR: Could not establish TCP connection");
        log_add("TCP connect failed");
#endif
        return;
    }

#if DAEMON_USE_TLS
    Serial.println("TLS connection established");
#else
    Serial.println("TCP connection established");
#endif

    Serial.println("\n=== HTTP REQUEST ===");
    String request = String("GET ") + DAEMON_PATH + " HTTP/1.1\r\n" +
                     "Host: " + daemon_ip + "\r\n" +
                     "Connection: close\r\n" +
                     "User-Agent: ESP32-M5Fire\r\n" +
                     "\r\n";

    client.print(request);
    Serial.println("Request sent");

    Serial.println("\n=== WAITING FOR RESPONSE ===");
    unsigned long start = millis();
    while (!client.available() && millis() - start < 5000) {
        delay(10);
    }

    if (!client.available()) {
        Serial.println("TIMEOUT: No response from server after 5 seconds");
        log_add("HTTP timeout waiting");
        client.stop();
        return;
    }

    Serial.println("Response received:");
    Serial.println("\n=== HTTP STATUS LINE ===");
    String statusLine = client.readStringUntil('\n');
    Serial.println(statusLine);
    if (statusLine.indexOf("200") >= 0) log_add("HTTP 200 OK");
    else log_format("HTTP %s", statusLine.c_str());

    Serial.println("\n=== RESPONSE HEADERS ===");
    int lineCount = 0;
    while (client.available() && lineCount < 10) {
        String line = client.readStringUntil('\n');
        if (line.length() == 0 || line[0] == '\r') {
            Serial.println("(end of headers)");
            break;
        }
        Serial.println(line);
        lineCount++;
    }

    Serial.println("\n=== RESPONSE BODY PREVIEW ===");
    String body = "";
    while (client.available() && body.length() < 500) {
        char c = client.read();
        body += c;
    }
    if (client.available()) {
        Serial.println("(truncated)");
    }
    Serial.println(body);

    client.stop();
    Serial.println("\n=== DEBUG END ===\n");
}

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

void http_client_debug_network(void) {
    debug_network_status();
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

#if DAEMON_USE_TLS
    if (http_client.begin(secure_client, mac_url)) {
#else
    if (http_client.begin(mac_url)) {
#endif
        int httpCode = http_client.GET();
        log_format("GET %d", httpCode);

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
    // Send POST to /api/refresh to trigger immediate scrape on daemon
    if (!http_client_is_connected()) {
        Serial.println("DEBUG: Can't refresh, WiFi not connected");
        return;
    }

    Serial.printf("DEBUG: Requesting daemon refresh at %s\n", refresh_url.c_str());

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
            Serial.println("DEBUG: Refresh requested successfully");
            log_add("Refresh requested");
        } else {
            Serial.printf("DEBUG: Refresh request failed (HTTP %d)\n", httpCode);
            log_format("Refresh HTTP %d", httpCode);
        }

        http_client.end();
    }
}
