#include "app_state.h"
#include "wifi.h"
#include "time_svc.h"
#include "display.h"
#include "button.h"
#include "sensor_dht.h"
#include "buzzer.h" 
#include "esp_log.h"
#include "nvs_flash.h"
#include "alarm_task.h"
#include "ble_alarm.h"  
#include "nvs_flash.h"
void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    ESP_ERROR_CHECK(ble_alarm_init());
    time_svc_init();
    display_hw_init();
    wifi_start_task();
    time_svc_start_tasks();
    sensor_dht_start_task();
    button_init_and_start();
    display_start_task();
    alarm_start_task();

    ESP_LOGI("main", "Initialization done - tasks started.");
}
