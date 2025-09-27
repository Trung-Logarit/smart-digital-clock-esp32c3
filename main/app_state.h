#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

#include "driver/gpio.h"
#include "max7219.h"


#define WIFI_SSID "LORION"
#define WIFI_PASS "777777777"


#define BUTTON_GPIO    GPIO_NUM_9     
#define BUTTON2_GPIO   GPIO_NUM_10   
#define BUTTON3_GPIO   GPIO_NUM_8     
#define BUTTON4_GPIO   GPIO_NUM_2     
#define DEBOUNCE_MS    250
#define ALARM_LED_GPIO        GPIO_NUM_4
#define ALARM_LED_ACTIVE_LEVEL 1


#define PIN_MOSI GPIO_NUM_7
#define PIN_SCLK GPIO_NUM_6
#define PIN_CS   GPIO_NUM_5


#define SENSOR_TYPE  DHT_TYPE_DHT11
#define DHT_GPIO_PIN GPIO_NUM_3


#define BUZZER_GPIO            GPIO_NUM_1
#define BUZZER_ACTIVE_LEVEL    1  


typedef enum {
    MODE_TIME = 0,     
    MODE_WDAY,         
    MODE_DDMM,         
    MODE_YYYY,         
    MODE_DHT,          
    MODE_SW,           
    MODE_ALARM_SET,    
    MODE_COUNTDOWN_SET,
    MODE_COUNTDOWN_RUN 
} display_mode_t;


typedef enum {
    SW_RESET_SHOWN = 0, 
    SW_RUNNING,        
    SW_PAUSED           
} sw_state_t;

typedef enum {
    ALARM_SEL_HOUR = 0,
    ALARM_SEL_MIN
} alarm_sel_t;


typedef enum {
    CD_SEL_MIN = 0,
    CD_SEL_SEC
} countdown_sel_t;

extern volatile display_mode_t s_mode;

extern max7219_t g_dev;

extern struct tm g_tm;
extern SemaphoreHandle_t g_time_mutex;

extern float s_temperature;
extern float s_humidity;

extern EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0


extern volatile sw_state_t s_sw_state;
extern volatile int  s_sw_mm, s_sw_ss;   
extern volatile bool s_force_refresh;    


extern volatile bool        s_alarm_enabled;
extern volatile int         s_alarm_hour, s_alarm_min;
extern volatile bool        s_alarm_ringing;
extern volatile alarm_sel_t s_alarm_sel;
extern volatile bool        s_blink_on;  


extern volatile int  s_cd_min, s_cd_sec;      
extern volatile countdown_sel_t s_cd_sel;     
extern volatile bool s_cd_running;           


static inline void time_set_timezone_vn(void) {
    setenv("TZ", "ICT-7", 1); // UTC+7
    tzset();
}
