#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif


void alarm_start_task(void);

void alarm_send_click_beep(void);

void alarm_send_confirm_beep(void);

void alarm_send_stop_ring(void);
void alarm_cmd_stop(void);
#ifdef __cplusplus
}
#endif
