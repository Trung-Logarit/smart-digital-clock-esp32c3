#include <string.h>
#include "app_state.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "max7219.h"
#include "time_svc.h"

static const char *TAGD = "display";


static inline uint8_t flip_byte(uint8_t b) {
    b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
    b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
    b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
    return b;
}

static inline uint8_t shift_down(uint8_t b) {
    return b; 
}

static void draw_cols_8x32(max7219_t *dev, const uint8_t cols[32]) {
    for (int m = 0; m < 4; m++) {
        uint8_t block[8];
        memcpy(block, &cols[m * 8], 8);
        max7219_draw_image_8x8(dev, m * 8, block);
    }
}


static void digit_to_cols(uint8_t d, uint8_t out8[8]) {
    static const uint8_t F[10][7] = {
        {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},
        {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
        {0x0E,0x11,0x01,0x06,0x08,0x10,0x1F},
        {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E},
        {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},
        {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
        {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},
        {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
        {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
        {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
    };
    for (int i = 0; i < 7; ++i) out8[i] = shift_down(F[d][i]);
    out8[7] = 0x00;
}


static void letter_to_cols(char ch, uint8_t out6[6]) {
    static const uint8_t A2Z[26][5] = {
        {0x0E,0x11,0x1F,0x11,0x11},{0x1E,0x11,0x1E,0x11,0x1E},{0x0E,0x11,0x10,0x11,0x0E},
        {0x1E,0x11,0x11,0x11,0x1E},{0x1F,0x10,0x1E,0x10,0x1F},{0x1F,0x10,0x1E,0x10,0x10},
        {0x0F,0x10,0x17,0x11,0x0F},{0x11,0x11,0x1F,0x11,0x11},{0x0E,0x04,0x04,0x04,0x0E},
        {0x01,0x01,0x01,0x11,0x0E},{0x11,0x12,0x1C,0x12,0x11},{0x10,0x10,0x10,0x10,0x1F},
        {0x11,0x1B,0x15,0x11,0x11},{0x11,0x19,0x15,0x13,0x11},{0x0E,0x11,0x11,0x11,0x0E},
        {0x1E,0x11,0x1E,0x10,0x10},{0x0E,0x11,0x11,0x15,0x0E},{0x1E,0x11,0x1E,0x12,0x11},
        {0x0F,0x10,0x0E,0x01,0x1E},{0x1F,0x04,0x04,0x04,0x04},{0x11,0x11,0x11,0x11,0x0E},
        {0x11,0x11,0x0A,0x0A,0x04},{0x11,0x11,0x15,0x1B,0x11},{0x11,0x0A,0x04,0x0A,0x11},
        {0x11,0x0A,0x04,0x04,0x04},{0x1F,0x02,0x04,0x08,0x1F},
    };
    if (ch >= 'A' && ch <= 'Z') {
        const uint8_t *p = A2Z[ch - 'A'];
        for (int i = 0; i < 5; ++i) out6[i] = shift_down(p[i]);
        out6[5] = 0;
    } else {
        for (int i = 0; i < 6; ++i) out6[i] = 0;
    }
}


static void draw_time_HHMM(max7219_t *dev, int hour, int minute) {
    uint8_t cols[32] = {0}, d8[4][8];
    digit_to_cols((hour / 10) % 10, d8[0]);
    digit_to_cols(hour % 10, d8[1]);
    digit_to_cols((minute / 10) % 10, d8[2]);
    digit_to_cols(minute % 10, d8[3]);
    memcpy(&cols[0], d8[0], 8); memcpy(&cols[8], d8[1], 8);
    memcpy(&cols[16], d8[2], 8); memcpy(&cols[24], d8[3], 8);
    draw_cols_8x32(dev, cols);
}

static void draw_number_4digits(max7219_t *dev, int n0, int n1, int n2, int n3) {
    uint8_t cols[32] = {0}, d8[4][8];
    digit_to_cols(n0, d8[0]); digit_to_cols(n1, d8[1]);
    digit_to_cols(n2, d8[2]); digit_to_cols(n3, d8[3]);
    memcpy(&cols[0], d8[0], 8); memcpy(&cols[8], d8[1], 8);
    memcpy(&cols[16], d8[2], 8); memcpy(&cols[24], d8[3], 8);
    draw_cols_8x32(dev, cols);
}

static void draw_wday_3letters(max7219_t *dev, int wday) {
    static const char *WD[] = {"SUN","MON","TUE","WED","THU","FRI","SAT"};
    const char *s = (wday >= 0 && wday <= 6) ? WD[wday] : "SUN";
    uint8_t cols[32] = {0}; uint8_t L0[6], L1[6], L2[6];
    letter_to_cols(s[0], L0); letter_to_cols(s[1], L1); letter_to_cols(s[2], L2);
    memcpy(&cols[0*8+1], L0, 6); memcpy(&cols[1*8+1], L1, 6); memcpy(&cols[2*8+1], L2, 6);
    draw_cols_8x32(dev, cols);
}

static void draw_alarm_HHMM_blink(max7219_t *dev, int hour, int minute, bool blink_on, alarm_sel_t sel) {
    uint8_t cols[32] = {0}, d8[4][8];
    digit_to_cols((hour / 10) % 10, d8[0]);
    digit_to_cols(hour % 10, d8[1]);
    digit_to_cols((minute / 10) % 10, d8[2]);
    digit_to_cols(minute % 10, d8[3]);

    if (!blink_on) {
        if (sel == ALARM_SEL_HOUR) { memset(d8[0], 0, 8); memset(d8[1], 0, 8); }
        else                       { memset(d8[2], 0, 8); memset(d8[3], 0, 8); }
    }

    memcpy(&cols[0], d8[0], 8);  memcpy(&cols[8], d8[1], 8);
    memcpy(&cols[16], d8[2], 8); memcpy(&cols[24], d8[3], 8);
    draw_cols_8x32(dev, cols);
}


static void draw_countdown_MMSS_blink(max7219_t *dev, int mm, int ss, bool blink_on, countdown_sel_t sel) {
    uint8_t cols[32] = {0}, d8[4][8];
    digit_to_cols((mm / 10) % 10, d8[0]);
    digit_to_cols(mm % 10,       d8[1]);
    digit_to_cols((ss / 10) % 10, d8[2]);
    digit_to_cols(ss % 10,        d8[3]);

    if (!blink_on) {
        if (sel == CD_SEL_MIN) { memset(d8[0], 0, 8); memset(d8[1], 0, 8); }
        else                   { memset(d8[2], 0, 8); memset(d8[3], 0, 8); }
    }

    memcpy(&cols[0], d8[0], 8);  memcpy(&cols[8],  d8[1], 8);
    memcpy(&cols[16], d8[2], 8); memcpy(&cols[24], d8[3], 8);
    draw_cols_8x32(dev, cols);
}


static void display_task(void *arg) {
    int last_min = -1;
    display_mode_t last_mode = (display_mode_t)255;
    TickType_t sw_last_tick = xTaskGetTickCount();
    TickType_t cd_last_tick = xTaskGetTickCount();
    int prev_sw_mm = -1, prev_sw_ss = -1;

    TickType_t last_blink = xTaskGetTickCount();
    const TickType_t blink_interval = pdMS_TO_TICKS(500);

    while (1) {
        struct tm tm_local;
        time_svc_get_localtime(&tm_local);

        if (s_mode == MODE_SW && s_sw_state == SW_RUNNING) {
            TickType_t now = xTaskGetTickCount();
            if ((now - sw_last_tick) >= pdMS_TO_TICKS(1000)) {
                sw_last_tick = now;
                int mm = s_sw_mm, ss = s_sw_ss;
                ss++; if (ss >= 60) { ss = 0; mm++; }
                if (mm >= 100) { mm = 0; ss = 0; }
                s_sw_mm = mm; s_sw_ss = ss;
                s_force_refresh = true;
            }
        } else {
            sw_last_tick = xTaskGetTickCount();
        }

        if (s_mode == MODE_COUNTDOWN_RUN && s_cd_running) {
            TickType_t now = xTaskGetTickCount();
            if ((now - cd_last_tick) >= pdMS_TO_TICKS(1000)) {
                cd_last_tick = now;

                int mm = s_cd_min, ss = s_cd_sec;
                if (mm == 0 && ss == 0) {
                    if (!s_alarm_ringing) {
                        s_alarm_ringing = true;   
                    }
                    s_cd_running = false;
                } else {
                    if (ss == 0) { ss = 59; if (mm > 0) mm--; }
                    else { ss--; }
                    s_cd_min = mm; s_cd_sec = ss;
                }
                s_force_refresh = true;
            }
        } else {
            cd_last_tick = xTaskGetTickCount();
        }

        if (s_mode == MODE_ALARM_SET || s_mode == MODE_COUNTDOWN_SET) {
            TickType_t now = xTaskGetTickCount();
            if ((now - last_blink) >= blink_interval) {
                last_blink = now;
                s_blink_on = !s_blink_on;
                s_force_refresh = true;
            }
        } else {
            s_blink_on = true;
        }

        bool need_refresh = s_force_refresh;
        s_force_refresh = false;

        if (s_mode != last_mode) {
            need_refresh = true;
            last_mode = s_mode;
            last_min = -1;
            prev_sw_mm = -1; prev_sw_ss = -1;
        }
        if (s_mode == MODE_TIME && tm_local.tm_min != last_min) {
            need_refresh = true;
            last_min = tm_local.tm_min;
        }
        if (s_mode == MODE_SW && (s_sw_mm != prev_sw_mm || s_sw_ss != prev_sw_ss)) {
            need_refresh = true;
            prev_sw_mm = s_sw_mm; prev_sw_ss = s_sw_ss;
        }

        if (need_refresh) {
            switch (s_mode) {
            case MODE_TIME:
                draw_time_HHMM(&g_dev, tm_local.tm_hour, tm_local.tm_min);
                printf("%02d%02d\n", tm_local.tm_hour, tm_local.tm_min);
                break;

            case MODE_WDAY:
                draw_wday_3letters(&g_dev, tm_local.tm_wday);
                {
                    static const char *W[]={"SUN","MON","TUE","WED","THU","FRI","SAT"};
                    printf("%s\n", W[(tm_local.tm_wday>=0&&tm_local.tm_wday<=6)?tm_local.tm_wday:0]);
                }
                break;

            case MODE_DDMM:
                draw_number_4digits(&g_dev,
                    (tm_local.tm_mday/10)%10, tm_local.tm_mday%10,
                    ((tm_local.tm_mon+1)/10)%10, (tm_local.tm_mon+1)%10);
                printf("%02d%02d\n", tm_local.tm_mday, tm_local.tm_mon+1);
                break;

            case MODE_YYYY: {
                int y = tm_local.tm_year + 1900;
                draw_number_4digits(&g_dev,(y/1000)%10,(y/100)%10,(y/10)%10,y%10);
                printf("%04d\n", y);
                break; }

            case MODE_DHT: {
                int t = (int)(s_temperature + 0.5f) * 30;
                int h = (int)(s_humidity + 0.5f) * 35;
                draw_number_4digits(&g_dev,(t/10)%10,t%10,(h/10)%10,h%10);
                break; }

            case MODE_SW:
                draw_number_4digits(&g_dev,
                    (s_sw_mm/10)%10, s_sw_mm%10,
                    (s_sw_ss/10)%10, s_sw_ss%10);
                printf("SW %02d%02d [%s]\n",
                       s_sw_mm, s_sw_ss,
                       (s_sw_state==SW_RUNNING)?"RUN":
                       (s_sw_state==SW_PAUSED) ?"PAUSE":"RST");
                break;

            case MODE_ALARM_SET:
                draw_alarm_HHMM_blink(&g_dev, s_alarm_hour, s_alarm_min, s_blink_on, s_alarm_sel);
                printf("ALARM SET %02d:%02d [%s%s]\n",
                       s_alarm_hour, s_alarm_min,
                       (s_alarm_sel==ALARM_SEL_HOUR)?"H":"M",
                       s_blink_on?"*":" ");
                break;

            case MODE_COUNTDOWN_SET:
                draw_countdown_MMSS_blink(&g_dev, s_cd_min, s_cd_sec, s_blink_on, s_cd_sel);
                printf("CD SET %02d:%02d [%s%s]\n",
                       s_cd_min, s_cd_sec,
                       (s_cd_sel==CD_SEL_MIN)?"M":"S",
                       s_blink_on?"*":" ");
                break;

            case MODE_COUNTDOWN_RUN:
                draw_number_4digits(&g_dev,
                    (s_cd_min/10)%10, s_cd_min%10,
                    (s_cd_sec/10)%10, s_cd_sec%10);
                printf("CD RUN %02d:%02d\n", s_cd_min, s_cd_sec);
                break;

            default:
                break;
            }
            fflush(stdout);
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void display_hw_init(void) {
    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 0
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    memset(&g_dev, 0, sizeof(g_dev));
    ESP_ERROR_CHECK(max7219_init_desc(&g_dev, SPI2_HOST, 1000000, PIN_CS));
    g_dev.cascade_size = 4;
    g_dev.digits = 32;
    g_dev.mirrored = false;
    ESP_ERROR_CHECK(max7219_init(&g_dev));
    ESP_ERROR_CHECK(max7219_set_brightness(&g_dev, 8));
    ESP_LOGI(TAGD, "Display init done");
}

void display_start_task(void) {
    xTaskCreate(display_task, "display_task", 4096, NULL, 5, NULL);
}
