#pragma once
#include "data.h"

enum fire_link_state_t {
    FIRE_LINK_UNUSED = 0,
};

enum screen_t {
    SCREEN_SPLASH,
    SCREEN_USAGE,
    SCREEN_BLUETOOTH,
    SCREEN_COUNT,
};

void ui_init(void);
void ui_update(const UsageData* data);
void ui_tick_anim(void);
void ui_show_screen(screen_t screen);
void ui_cycle_screen(void);
void ui_toggle_splash(void);
screen_t ui_get_current_screen(void);
void ui_update_ble_status(fire_link_state_t state, const char* name, const char* mac);
void ui_update_battery(int percent, bool charging);
