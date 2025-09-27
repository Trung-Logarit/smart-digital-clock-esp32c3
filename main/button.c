#include "app_state.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/queue.h"
#include "time_svc.h"
#include "alarm_task.h"

static const char *TAGB = "button";
static QueueHandle_t gpio_evt_queue = NULL;

typedef struct { uint32_t gpio; int64_t t_us; } btn_evt_t;


static volatile int64_t s_last_btn1_us = -1;
static volatile int64_t s_last_btn2_us = -1;
static volatile int64_t s_last_btn3_us = -1;
static volatile int64_t s_last_btn4_us = -1;


static volatile int64_t s_btn3_press_us = -1;
static volatile int64_t s_btn4_press_us = -1;

static const int64_t HOLD_CONFIRM_US = 1000000; 


static void handle_button1_normal(void);
static void handle_button2_normal(void);

static inline int wrap(int v, int lo, int hi);
static void alarm_enter_or_toggle_field(void);
static void alarm_confirm_if_holding(int64_t held_us);
static void handle_btn1_alarm(void);
static void handle_btn2_alarm(void);


static void cd_enter_or_toggle_field(void);
static void cd_confirm_if_holding(int64_t held_us);
static void handle_btn1_cd(void);
static void handle_btn2_cd(void);



static inline bool is_active_low_pressed(gpio_num_t gpio) {
    return gpio_get_level(gpio) == 0;
}

static void IRAM_ATTR button_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    btn_evt_t e = { .gpio = gpio_num, .t_us = esp_timer_get_time() };
    BaseType_t hpw = pdFALSE;
    if (gpio_evt_queue) {
        xQueueSendFromISR(gpio_evt_queue, &e, &hpw);
        if (hpw) portYIELD_FROM_ISR();
    }
}


static void handle_button1_normal(void)
{
 
    if (s_mode == MODE_ALARM_SET) return;

    if (s_mode == MODE_SW) {
        s_mode = MODE_TIME;
        s_force_refresh = true;
        return;
    }

    s_mode = (display_mode_t)((s_mode + 1) % MODE_SW);
    if (s_mode == MODE_SW) s_mode = MODE_TIME;
    s_force_refresh = true;
}


static void handle_button2_normal(void)
{
    if (s_mode != MODE_SW) {
        s_mode = MODE_SW;
        s_sw_state = SW_RESET_SHOWN;
        s_sw_mm = 0; s_sw_ss = 0;
        s_force_refresh = true;
        return;
    }

    if (s_sw_state == SW_RESET_SHOWN)      s_sw_state = SW_RUNNING;
    else if (s_sw_state == SW_RUNNING)     s_sw_state = SW_PAUSED;
    else /* PAUSED */ {
        s_sw_state = SW_RESET_SHOWN;
        s_sw_mm = 0; s_sw_ss = 0;
    }
    s_force_refresh = true;
}

static inline int wrap(int v, int lo, int hi) {
    if (v < lo) return hi;
    if (v > hi) return lo;
    return v;
}


static void alarm_enter_or_toggle_field(void)
{
    if (s_alarm_ringing) {
        s_alarm_ringing = false;
        alarm_send_stop_ring();
        ESP_LOGW(TAGB, "Alarm stopped by BTN3.");
        return;
    }

    if (s_mode != MODE_ALARM_SET) {
        struct tm nowtm;
        time_svc_get_localtime(&nowtm);
        s_alarm_hour = nowtm.tm_hour;
        s_alarm_min  = nowtm.tm_min;

        s_mode = MODE_ALARM_SET;
        s_alarm_sel = ALARM_SEL_HOUR;  
        s_blink_on = true;
        s_force_refresh = true;
        ESP_LOGI(TAGB, "Enter ALARM SET from %02d:%02d (edit HOUR).",
                 s_alarm_hour, s_alarm_min);
    } else {
        s_alarm_sel = (s_alarm_sel == ALARM_SEL_HOUR) ? ALARM_SEL_MIN : ALARM_SEL_HOUR;
        s_force_refresh = true;
        ESP_LOGI(TAGB, "Switch edit field: %s", (s_alarm_sel==ALARM_SEL_HOUR)?"HOUR":"MIN");
    }
}

static void alarm_confirm_if_holding(int64_t held_us)
{
    if (held_us >= HOLD_CONFIRM_US && s_mode == MODE_ALARM_SET) {
        s_alarm_enabled = true;
        s_force_refresh = true;
        ESP_LOGI(TAGB, "Alarm saved: %02d:%02d", s_alarm_hour, s_alarm_min);

        alarm_send_confirm_beep();

        s_mode = MODE_TIME;
        s_force_refresh = true;
    }
}

static void handle_btn1_alarm(void) {   
    if (s_alarm_sel == ALARM_SEL_HOUR) s_alarm_hour = wrap(s_alarm_hour + 1, 0, 23);
    else                                s_alarm_min  = wrap(s_alarm_min  + 1, 0, 59);
    s_force_refresh = true;
}

static void handle_btn2_alarm(void) {   
    if (s_alarm_sel == ALARM_SEL_HOUR) s_alarm_hour = wrap(s_alarm_hour - 1, 0, 23);
    else                                s_alarm_min  = wrap(s_alarm_min  - 1, 0, 59);
    s_force_refresh = true;
}

static void cd_enter_or_toggle_field(void)
{
    if (s_alarm_ringing) {
        s_alarm_ringing = false;
        alarm_send_stop_ring();
        ESP_LOGW(TAGB, "Ring stopped by BTN4.");

        if (s_mode == MODE_COUNTDOWN_RUN && !s_cd_running) {
            s_mode = MODE_TIME;
            s_force_refresh = true;
        }
        return;
    }

    if (s_mode != MODE_COUNTDOWN_SET && s_mode != MODE_COUNTDOWN_RUN) {
        s_cd_min = 15; s_cd_sec = 0;
        s_cd_running = false;
        s_cd_sel = CD_SEL_MIN;
        s_mode = MODE_COUNTDOWN_SET;
        s_blink_on = true;
        s_force_refresh = true;
        ESP_LOGI(TAGB, "Enter COUNTDOWN SET (15:00) edit MIN.");
    } else if (s_mode == MODE_COUNTDOWN_SET) {
        s_cd_sel = (s_cd_sel == CD_SEL_MIN) ? CD_SEL_SEC : CD_SEL_MIN;
        s_force_refresh = true;
        ESP_LOGI(TAGB, "COUNTDOWN toggle field: %s", (s_cd_sel==CD_SEL_MIN)?"MIN":"SEC");
    } else {
    }
}

static void cd_confirm_if_holding(int64_t held_us)
{
    if (held_us >= HOLD_CONFIRM_US && s_mode == MODE_COUNTDOWN_SET) {
        s_cd_running = true;
        s_mode = MODE_COUNTDOWN_RUN;
        s_force_refresh = true;
        alarm_send_confirm_beep();  
        ESP_LOGI(TAGB, "COUNTDOWN started: %02d:%02d", s_cd_min, s_cd_sec);
    }
}

static void handle_btn1_cd(void) {  
    if (s_cd_sel == CD_SEL_MIN) s_cd_min = wrap(s_cd_min + 1, 0, 99);
    else                        s_cd_sec = wrap(s_cd_sec + 1, 0, 59);
    s_force_refresh = true;
}

static void handle_btn2_cd(void) {  
    if (s_cd_sel == CD_SEL_MIN) s_cd_min = wrap(s_cd_min - 1, 0, 99);
    else                        s_cd_sec = wrap(s_cd_sec - 1, 0, 59);
    s_force_refresh = true;
}


static void button_task(void *arg)
{
    btn_evt_t e;
    const int64_t debounce_us = (int64_t)DEBOUNCE_MS * 1000LL;

    while (1) {
        if (xQueueReceive(gpio_evt_queue, &e, portMAX_DELAY)) {
            bool is_press = (gpio_get_level(e.gpio) == 0);  

            if (e.gpio == BUTTON_GPIO) {
                if (s_last_btn1_us < 0 || (e.t_us - s_last_btn1_us) > debounce_us) {
                    s_last_btn1_us = e.t_us;
                    if (is_press) {
                        alarm_send_click_beep();
                        if      (s_mode == MODE_ALARM_SET)         handle_btn1_alarm();
                        else if (s_mode == MODE_COUNTDOWN_SET)     handle_btn1_cd();
                        else                                        handle_button1_normal();
                        ESP_LOGI(TAGB, "BTN1 press");
                    }
                }

            } else if (e.gpio == BUTTON2_GPIO) {
                if (s_last_btn2_us < 0 || (e.t_us - s_last_btn2_us) > debounce_us) {
                    s_last_btn2_us = e.t_us;
                    if (is_press) {
                        alarm_send_click_beep();
                        if      (s_mode == MODE_ALARM_SET)         handle_btn2_alarm();
                        else if (s_mode == MODE_COUNTDOWN_SET)     handle_btn2_cd();
                        else                                        handle_button2_normal();
                        ESP_LOGI(TAGB, "BTN2 press");
                    }
                }

            } else if (e.gpio == BUTTON3_GPIO) {
                if (is_press) {
                    if (s_last_btn3_us < 0 || (e.t_us - s_last_btn3_us) > debounce_us) {
                        s_last_btn3_us = e.t_us;
                        s_btn3_press_us = e.t_us; 
                        alarm_send_click_beep();
                        alarm_enter_or_toggle_field();
                        ESP_LOGI(TAGB, "BTN3 press");
                    }
                } else {
                    if (s_btn3_press_us > 0) {
                        int64_t held = e.t_us - s_btn3_press_us;
                        s_btn3_press_us = -1;
                        alarm_confirm_if_holding(held);
                        ESP_LOGI(TAGB, "BTN3 release held=%lldms", (long long)(held/1000));
                    }
                }

            } else if (e.gpio == BUTTON4_GPIO) {
                if (is_press) {
                    if (s_last_btn4_us < 0 || (e.t_us - s_last_btn4_us) > debounce_us) {
                        s_last_btn4_us = e.t_us;
                        s_btn4_press_us = e.t_us; 
                        alarm_send_click_beep();
                        cd_enter_or_toggle_field();   
                        ESP_LOGI(TAGB, "BTN4 press");
                    }
                } else {
                    if (s_btn4_press_us > 0) {
                        int64_t held = e.t_us - s_btn4_press_us;
                        s_btn4_press_us = -1;
                        cd_confirm_if_holding(held); 
                        ESP_LOGI(TAGB, "BTN4 release held=%lldms", (long long)(held/1000));
                    }
                }
            }
        }
    }
}

void button_init_and_start(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << BUTTON_GPIO) |
                        (1ULL << BUTTON2_GPIO) |
                        (1ULL << BUTTON3_GPIO) |
                        (1ULL << BUTTON4_GPIO),  
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE
    };
    ESP_ERROR_CHECK(gpio_config(&io));

    gpio_evt_queue = xQueueCreate(10, sizeof(btn_evt_t));
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(BUTTON_GPIO,  button_isr_handler, (void *)(uint32_t)BUTTON_GPIO));
    ESP_ERROR_CHECK(gpio_isr_handler_add(BUTTON2_GPIO, button_isr_handler, (void *)(uint32_t)BUTTON2_GPIO));
    ESP_ERROR_CHECK(gpio_isr_handler_add(BUTTON3_GPIO, button_isr_handler, (void *)(uint32_t)BUTTON3_GPIO));
    ESP_ERROR_CHECK(gpio_isr_handler_add(BUTTON4_GPIO, button_isr_handler, (void *)(uint32_t)BUTTON4_GPIO)); // THÊM BTN4

    xTaskCreate(button_task, "button_task", 3072, NULL, 10, NULL);
}
