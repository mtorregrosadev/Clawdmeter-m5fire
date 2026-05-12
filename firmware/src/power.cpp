#include "power.h"
#include <M5Unified.h>

static int cached_pct = -1;
static bool cached_charging = false;

void power_init(void) {
    cached_pct = M5.Power.getBatteryLevel();
    cached_charging = M5.Power.isCharging();
}

void power_tick(void) {
    cached_pct = M5.Power.getBatteryLevel();
    cached_charging = M5.Power.isCharging();
}

int power_battery_pct(void) {
    return cached_pct;
}

bool power_is_charging(void) {
    return cached_charging;
}

bool power_pwr_pressed(void) {
    return false;
}
