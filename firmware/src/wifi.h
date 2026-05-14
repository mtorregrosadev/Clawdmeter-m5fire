#pragma once

enum wifi_state_t {
    WIFI_IDLE,        // Waiting for setup
    WIFI_CONNECTING,  // Attempting to connect
    WIFI_CONNECTED,   // Connected to WiFi
    WIFI_FAILED,      // Connection failed
};

void wifi_init(void);
void wifi_tick(void);
wifi_state_t wifi_get_state(void);
const char* wifi_get_ssid(void);
int wifi_get_signal_strength(void);
const char* wifi_get_ip(void);
bool wifi_is_connected(void);
