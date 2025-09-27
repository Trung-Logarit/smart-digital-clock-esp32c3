#pragma once
#include <stdint.h>
#include "esp_err.h"


esp_err_t ble_alarm_init(void);


void ble_alarm_notify_ringing(uint8_t ringing);


void ble_alarm_refresh_alarm_time(void);
