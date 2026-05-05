#pragma once

#include <stdbool.h>

void app_status_led_init(void);
void app_status_led_indicate_setup_mode(void);
void app_status_led_indicate_complete(void);
void app_status_led_indicate_factory_reset(void);
void app_status_led_indicate_wifi_reset(void);
void app_status_led_indicate_switch(bool on);
