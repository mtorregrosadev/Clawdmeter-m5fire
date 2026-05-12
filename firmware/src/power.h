#pragma once

void power_init(void);
void power_tick(void);
int power_battery_pct(void);
bool power_is_charging(void);
bool power_pwr_pressed(void);
