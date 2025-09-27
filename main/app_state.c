#include "app_state.h"


volatile display_mode_t s_mode = MODE_TIME;


max7219_t g_dev = {0};

struct tm g_tm = {0};
SemaphoreHandle_t g_time_mutex = NULL;

float s_temperature = 0.0f;
float s_humidity = 0.0f;


EventGroupHandle_t s_wifi_event_group = NULL;

volatile sw_state_t s_sw_state = SW_RESET_SHOWN;
volatile int s_sw_mm = 0;
volatile int s_sw_ss = 0;
volatile bool s_force_refresh = false;

volatile bool s_alarm_enabled = false;
volatile int  s_alarm_hour = 7;
volatile int  s_alarm_min  = 0;
volatile bool s_alarm_ringing = false;
volatile alarm_sel_t s_alarm_sel = ALARM_SEL_HOUR;
volatile bool s_blink_on = true;


volatile int  s_cd_min = 7;          
volatile int  s_cd_sec = 7;
volatile countdown_sel_t s_cd_sel = CD_SEL_MIN;
volatile bool s_cd_running = false;
