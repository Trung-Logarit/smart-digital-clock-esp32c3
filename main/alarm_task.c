#include "alarm_task.h"
#include "app_state.h"
#include "time_svc.h"
#include "buzzer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "led.h"
#include "ble_alarm.h"

static const char *TAGA = "alarm_task";

static const int s_sos_durations_ms[] = {
    200,200, 200,200, 200,600,
    600,200, 600,200, 600,600,
    200,200, 200,200, 200,1000
};
static const bool s_sos_on_step[] = {
    true,false, true,false, true,false,
    true,false, true,false, true,false,
    true,false, true,false, true,false
};
static const int s_sos_steps = sizeof(s_sos_durations_ms)/sizeof(s_sos_durations_ms[0]);
static int s_sos_idx = 0;
static int64_t s_sos_next_us = 0;

typedef enum {
    ALARM_CMD_CLICK_BEEP = 0,
    ALARM_CMD_CONFIRM_BEEP,
    ALARM_CMD_STOP_RING
} alarm_cmd_t;

static QueueHandle_t s_alarm_q = NULL;

static bool s_click_active = false;
static int64_t s_click_until_us = 0;

static bool s_confirm_active = false;
static int64_t s_confirm_until_us = 0;

static inline int64_t now_us(void) { return esp_timer_get_time(); }

static void buzzer_arbitrate_output(void)
{
    if (s_alarm_ringing || s_confirm_active || s_click_active) {
        buzzer_on();
    } else {
        buzzer_off();
    }
}

static void alarm_mgr_task(void *arg)
{
    buzzer_init();
    alarm_led_init();
    s_alarm_q = xQueueCreate(8, sizeof(alarm_cmd_t));

    int last_checked_sec = -1;

    while (1) {
        alarm_cmd_t cmd;
        while (xQueueReceive(s_alarm_q, &cmd, 0) == pdTRUE) {
            switch (cmd) {
            case ALARM_CMD_CLICK_BEEP:
                if (!s_alarm_ringing && !s_confirm_active) {
                    s_click_active = true;
                    s_click_until_us = now_us() + 30000;
                }
                break;
            case ALARM_CMD_CONFIRM_BEEP:
                s_click_active = false;
                s_confirm_active = true;
                s_confirm_until_us = now_us() + 1000000;
                break;
            case ALARM_CMD_STOP_RING:
                s_alarm_ringing = false;
                s_sos_idx = 0;
                s_sos_next_us = 0;
                ble_alarm_notify_ringing(0);  
                break;
            }
        }

        struct tm tmv;
        time_svc_get_localtime(&tmv);

        if (tmv.tm_sec != last_checked_sec) {
            last_checked_sec = tmv.tm_sec;

            if (s_alarm_enabled && !s_alarm_ringing &&
                tmv.tm_hour == s_alarm_hour &&
                tmv.tm_min  == s_alarm_min &&
                tmv.tm_sec  == 0)
            {
                s_alarm_ringing = true;
                ESP_LOGW(TAGA, "ALARM RING %02d:%02d !", s_alarm_hour, s_alarm_min);
                ble_alarm_notify_ringing(1); 
            }
        }
        int64_t t = now_us();
        if (s_click_active && t >= s_click_until_us) s_click_active = false;
        if (s_confirm_active && t >= s_confirm_until_us) s_confirm_active = false;

        buzzer_arbitrate_output();
        if (s_alarm_ringing) {
            int64_t tnow = now_us();
            if (tnow >= s_sos_next_us) {
                if (s_sos_on_step[s_sos_idx]) alarm_led_on();
                else                           alarm_led_off();

                int dur_ms = s_sos_durations_ms[s_sos_idx];
                s_sos_next_us = tnow + (int64_t)dur_ms * 1000LL;
                s_sos_idx = (s_sos_idx + 1) % s_sos_steps;
            }
        } else {
            alarm_led_off();
            s_sos_idx = 0;
            s_sos_next_us = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void alarm_start_task(void)
{
    xTaskCreate(alarm_mgr_task, "alarm_task", 3072, NULL, 6, NULL);
}

void alarm_send_click_beep(void)
{
    if (s_alarm_q) {
        alarm_cmd_t c = ALARM_CMD_CLICK_BEEP;
        (void)xQueueSend(s_alarm_q, &c, 0);
    }
}

void alarm_send_confirm_beep(void)
{
    if (s_alarm_q) {
        alarm_cmd_t c = ALARM_CMD_CONFIRM_BEEP;
        (void)xQueueSend(s_alarm_q, &c, 0);
    }
}

void alarm_send_stop_ring(void)
{
    if (s_alarm_q) {
        alarm_cmd_t c = ALARM_CMD_STOP_RING;
        (void)xQueueSend(s_alarm_q, &c, 0);
    }
}
