#pragma once

enum wifi_state_t {
    WIFI_IDLE,        // Waiting for setup
    WIFI_CONNECTING,  // Attempting to connect
    WIFI_GETTING_IP,  // Connected but waiting for IP assignment (DHCP)
    WIFI_CONNECTED,   // Connected to WiFi with valid IP
    WIFI_FAILED,      // Connection failed
};

void wifi_init(void);
void wifi_tick(void);
void wifi_reconfigure(void);
wifi_state_t wifi_get_state(void);
const char* wifi_get_ssid(void);
int wifi_get_signal_strength(void);
const char* wifi_get_ip(void);
bool wifi_is_connected(void);
