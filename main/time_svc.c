#include <string.h>
#include "app_state.h"

#include "esp_log.h"
#include "lwip/apps/sntp.h"
#include "lwip/ip_addr.h"

static const char *TAGT = "time";


static void sntp_set_server_with_fallback(int idx, const char *hostname, const char *ip_fallback)
{

    sntp_setservername(idx, (char *)hostname);

    if (ip_fallback && *ip_fallback) {
        ip_addr_t addr;
        if (ipaddr_aton(ip_fallback, &addr)) {
            int fb = (idx + 1) % SNTP_MAX_SERVERS;
            sntp_setserver(fb, &addr);
        }
    }
}


static bool try_time_sync_multi_servers(void)
{
    const struct { const char *host; const char *ip; } servers[] = {
        {"time.google.com", "216.239.35.0"},
        {"pool.ntp.org", "129.6.15.28"},
        {"0.vn.pool.ntp.org", "120.72.88.22"}};

    for (size_t i = 0; i < sizeof(servers)/sizeof(servers[0]); ++i) {
        sntp_stop();
        sntp_setoperatingmode(SNTP_OPMODE_POLL);
        sntp_set_server_with_fallback(0, servers[i].host, servers[i].ip);
        sntp_init();

        time_set_timezone_vn();

        for (int t = 0; t < 10; ++t) {
            time_t now = 0;
            struct tm tmv = {0};
            time(&now);
            localtime_r(&now, &tmv);
            if (tmv.tm_year >= (2020 - 1900)) {
                ESP_LOGI(TAGT, "Time synced via %s: %04d-%02d-%02d %02d:%02d:%02d",
                         servers[i].host, tmv.tm_year+1900, tmv.tm_mon+1, tmv.tm_mday,
                         tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
                return true;
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    return false;
}

static void time_task(void *arg)
{
    while (1) {
        time_t now;
        struct tm local;
        time(&now);
        localtime_r(&now, &local);

        if (g_time_mutex && xSemaphoreTake(g_time_mutex, pdMS_TO_TICKS(100))) {
            g_tm = local;
            xSemaphoreGive(g_time_mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void ntp_task(void *arg)
{
    if (s_wifi_event_group) {
        xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
    }

    if (!try_time_sync_multi_servers()) {
        ESP_LOGW(TAGT, "Time not synced initially; using local time until sync.");
    }

    const TickType_t delay_hour = pdMS_TO_TICKS(3600UL * 1000UL);
    while (1) {
        vTaskDelay(delay_hour);
        ESP_LOGI(TAGT, "Periodic NTP sync...");
        try_time_sync_multi_servers();
    }
}

void time_svc_init(void)
{
    if (!g_time_mutex) g_time_mutex = xSemaphoreCreateMutex();
    time_set_timezone_vn();
}

void time_svc_start_tasks(void)
{
    xTaskCreate(time_task, "time_task", 2048, NULL, 5, NULL);
    xTaskCreate(ntp_task,  "ntp_task",  4096, NULL, 5, NULL);
}

bool time_svc_get_localtime(struct tm *out)
{
    if (!out) return false;
    if (g_time_mutex && xSemaphoreTake(g_time_mutex, pdMS_TO_TICKS(50))) {
        *out = g_tm;
        xSemaphoreGive(g_time_mutex);
        return true;
    }
    time_t now; time(&now);
    localtime_r(&now, out);
    return true;
}
