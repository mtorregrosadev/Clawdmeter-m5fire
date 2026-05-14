#include "wifi.h"
#include "leds.h"
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_http_server.h>
#include <EEPROM.h>
#include <cstring>
#include <lwip/ip4_addr.h>

#define EEPROM_SIZE 256
#define SSID_OFFSET 0
#define SSID_LEN 32
#define PASS_OFFSET 32
#define PASS_LEN 64
#define CONNECT_RETRY_TIMEOUT_MS 3000  // Retry every 3 seconds

static wifi_state_t current_state = WIFI_IDLE;
static char saved_ssid[SSID_LEN] = {0};
static char saved_pass[PASS_LEN] = {0};
static char current_ip[20] = "0.0.0.0";
static uint32_t connect_start_ms = 0;
static const uint32_t CONNECT_TIMEOUT_MS = 30000;  // 30 sec for iOS hotspot
static httpd_handle_t server_handle = NULL;
static bool initialized = false;
static uint8_t connection_attempts = 0;
static uint32_t ip_assign_start_ms = 0;  // Track when IP assignment starts

static void load_credentials(void) {
    EEPROM.begin(EEPROM_SIZE);
    memset(saved_ssid, 0, sizeof(saved_ssid));
    memset(saved_pass, 0, sizeof(saved_pass));
    for (int i = 0; i < SSID_LEN - 1; i++) {
        uint8_t c = EEPROM.read(SSID_OFFSET + i);
        if (c == 0xFF || c == 0) break;
        saved_ssid[i] = c;
    }
    for (int i = 0; i < PASS_LEN - 1; i++) {
        uint8_t c = EEPROM.read(PASS_OFFSET + i);
        if (c == 0xFF || c == 0) break;
        saved_pass[i] = c;
    }
    EEPROM.end();
}

static void save_credentials(const char* ssid, const char* pass) {
    EEPROM.begin(EEPROM_SIZE);
    for (int i = 0; i < SSID_LEN; i++) {
        EEPROM.write(SSID_OFFSET + i, i < strlen(ssid) ? ssid[i] : 0);
    }
    for (int i = 0; i < PASS_LEN; i++) {
        EEPROM.write(PASS_OFFSET + i, i < strlen(pass) ? pass[i] : 0);
    }
    EEPROM.commit();
    EEPROM.end();
}

static esp_err_t handle_root(httpd_req_t *req) {
    const char* html = R"(<!DOCTYPE html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>ClawdMeter WiFi</title><style>
body{font-family:Arial;background:#1a1a1a;color:#fff;margin:0;padding:20px}
.container{max-width:400px;margin:50px auto;background:#2a2a2a;padding:30px;border-radius:10px}
h1{text-align:center;color:#0077cc;margin-top:0}
input{width:100%;padding:10px;margin:10px 0;box-sizing:border-box;background:#333;color:#fff;border:1px solid #0077cc;border-radius:5px}
button{width:100%;padding:10px;margin-top:20px;background:#0077cc;color:#fff;border:none;border-radius:5px;cursor:pointer}
</style></head><body><div class="container"><h1>ClawdMeter</h1>
<form method='POST' action='/configure'>
<input type='text' name='ssid' placeholder='WiFi Network' required autofocus>
<input type='password' name='pass' placeholder='Password' required>
<button>Connect</button>
</form></div></body></html>)";
    httpd_resp_send(req, html, strlen(html));
    return ESP_OK;
}

static esp_err_t handle_configure(httpd_req_t *req) {
    char buf[256] = {0};
    int recv_len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (recv_len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid");
        return ESP_FAIL;
    }

    char ssid[SSID_LEN] = {0};
    char pass[PASS_LEN] = {0};

    char* ssid_start = strstr(buf, "ssid=");
    char* pass_start = strstr(buf, "pass=");

    if (ssid_start && pass_start) {
        ssid_start += 5;
        pass_start += 5;

        // Parse SSID until & or end of string
        int i = 0;
        while (ssid_start[i] && ssid_start[i] != '&' && i < SSID_LEN - 1) {
            ssid[i] = ssid_start[i];
            i++;
        }

        // Parse password until & or end of string or CR/LF
        i = 0;
        while (pass_start[i] && pass_start[i] != '&' && pass_start[i] != '\r' && pass_start[i] != '\n' && i < PASS_LEN - 1) {
            pass[i] = pass_start[i];
            i++;
        }

        if (strlen(ssid) > 0) {
            save_credentials(ssid, pass);
            strlcpy(saved_ssid, ssid, sizeof(saved_ssid));
            strlcpy(saved_pass, pass, sizeof(saved_pass));
            Serial.printf("DEBUG: Saved credentials - SSID: %s, Pass: %s (len: %d)\n", ssid, pass, strlen(pass));
            Serial.println("DEBUG: Will attempt WiFi connection in 60 seconds (or when credentials received)");
            httpd_resp_send(req, (const char*)"<html><body>Connecting...</body></html>", -1);
            return ESP_OK;
        }
    }

    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid params");
    return ESP_FAIL;
}

static void start_ap(void) {
    if (server_handle) {
        httpd_stop(server_handle);
        server_handle = NULL;
    }

    current_state = WIFI_IDLE;
    connect_start_ms = millis();  // Reset timer for next retry
    led_set(LED_BLUE_BLINK);       // Indicate WiFi reconfiguration mode

    esp_wifi_set_mode(WIFI_MODE_AP);

    wifi_config_t ap_cfg = {};
    strcpy((char*)ap_cfg.ap.ssid, "ClawdMeter-Setup");
    strcpy((char*)ap_cfg.ap.password, "12345678");
    ap_cfg.ap.ssid_len = 16;
    ap_cfg.ap.channel = 1;
    ap_cfg.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    ap_cfg.ap.max_connection = 2;

    esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
    esp_wifi_start();

    // Configure AP IP address and DHCP
    esp_netif_t* ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap_netif) {
        esp_netif_dhcps_stop(ap_netif);
        esp_netif_ip_info_t ip_info = {};
        IP4_ADDR(&ip_info.ip, 192, 168, 1, 1);
        IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
        IP4_ADDR(&ip_info.gw, 192, 168, 1, 1);
        esp_netif_set_ip_info(ap_netif, &ip_info);
        esp_netif_dhcps_start(ap_netif);
    }

    httpd_config_t http_cfg = HTTPD_DEFAULT_CONFIG();
    http_cfg.max_open_sockets = 2;
    if (httpd_start(&server_handle, &http_cfg) == ESP_OK) {
        httpd_uri_t root = {.uri = "/", .method = HTTP_GET, .handler = handle_root};
        httpd_uri_t cfg = {.uri = "/configure", .method = HTTP_POST, .handler = handle_configure};
        httpd_register_uri_handler(server_handle, &root);
        httpd_register_uri_handler(server_handle, &cfg);
    }
}

static void connect_wifi(void) {
    if (strlen(saved_ssid) == 0) {
        start_ap();
        return;
    }

    current_state = WIFI_CONNECTING;
    connect_start_ms = millis();

    esp_wifi_set_mode(WIFI_MODE_STA);

    wifi_config_t sta_cfg = {};
    strcpy((char*)sta_cfg.sta.ssid, saved_ssid);
    strcpy((char*)sta_cfg.sta.password, saved_pass);

    esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
    esp_wifi_start();
    esp_wifi_connect();
}

void wifi_init(void) {
    if (initialized) return;
    initialized = true;

    load_credentials();

    esp_netif_init();
    esp_event_loop_create_default();

    // Create AP and STA network interfaces
    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    connect_wifi();
}

void wifi_reconfigure(void) {
    Serial.println("DEBUG: Starting WiFi reconfiguration (AP mode)");
    start_ap();
}

void wifi_tick(void) {
    if (current_state == WIFI_CONNECTING) {
        wifi_ap_record_t ap_info = {};
        uint8_t status = esp_wifi_sta_get_ap_info(&ap_info);

        if (status == ESP_OK) {
            current_state = WIFI_GETTING_IP;
            ip_assign_start_ms = millis();
            Serial.println("DEBUG: Connected to AP, waiting for IP assignment...");
        } else if (millis() - connect_start_ms > CONNECT_TIMEOUT_MS) {
            connection_attempts++;
            if (connection_attempts < 3) {
                Serial.printf("DEBUG: Connection timeout, retrying (attempt %d/3)\n", connection_attempts + 1);
                connect_start_ms = millis();
                esp_wifi_disconnect();
                esp_wifi_connect();
            } else {
                Serial.println("DEBUG: Connection failed after 3 attempts, back to AP mode");
                current_state = WIFI_FAILED;
            }
        }
    } else if (current_state == WIFI_GETTING_IP) {
        esp_netif_ip_info_t ip_info = {};
        esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif) {
            esp_netif_get_ip_info(netif, &ip_info);
            // Check if we have a valid IP (not 0.0.0.0)
            if (ip_info.ip.addr != 0) {
                current_state = WIFI_CONNECTED;
                connection_attempts = 0;
                snprintf(current_ip, sizeof(current_ip), "%d.%d.%d.%d",
                    (ip_info.ip.addr >> 0) & 0xFF,
                    (ip_info.ip.addr >> 8) & 0xFF,
                    (ip_info.ip.addr >> 16) & 0xFF,
                    (ip_info.ip.addr >> 24) & 0xFF);
                Serial.printf("DEBUG: Got IP address! IP: %s\n", current_ip);
            }
        }
        // Timeout waiting for IP (10 seconds should be enough for iOS)
        if (millis() - ip_assign_start_ms > 10000) {
            Serial.println("DEBUG: IP assignment timeout, going back to AP mode");
            current_state = WIFI_FAILED;
        }
    } else if (current_state == WIFI_FAILED) {
        if (millis() - connect_start_ms > CONNECT_TIMEOUT_MS + 2000) {
            start_ap();
        }
    } else if (current_state == WIFI_IDLE && server_handle) {
        if (strlen(saved_ssid) > 0 && millis() - connect_start_ms > 60000) {
            led_set(LED_OFF);
            connection_attempts = 0;
            Serial.printf("DEBUG: Attempting to connect to SSID: %s\n", saved_ssid);
            connect_wifi();
            connect_start_ms = millis();
        }
    }
}

wifi_state_t wifi_get_state(void) {
    return current_state;
}

const char* wifi_get_ssid(void) {
    return saved_ssid;
}

int wifi_get_signal_strength(void) {
    wifi_ap_record_t ap_info = {};
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        int rssi = ap_info.rssi;
        if (rssi >= -50) return 100;
        if (rssi <= -100) return 0;
        return 2 * (rssi + 100);
    }
    return 0;
}

const char* wifi_get_ip(void) {
    return current_ip;
}

bool wifi_is_connected(void) {
    wifi_ap_record_t ap_info = {};
    return current_state == WIFI_CONNECTED && esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK;
}
