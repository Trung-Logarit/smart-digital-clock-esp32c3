#include "app_state.h"
#include "esp_log.h"
#include "dht.h"

static const char *TAGS = "dht";

static void dht_task(void *pvParameters)
{
    while (1) {
        float temperature, humidity;
        if (dht_read_float_data(SENSOR_TYPE, DHT_GPIO_PIN, &humidity, &temperature) == ESP_OK) {
            s_temperature = temperature;
            s_humidity = humidity;
            ESP_LOGI(TAGS, "Humidity: %.1f%% Temp: %.1fC", s_humidity * 35, s_temperature * 30);
        } else {
            ESP_LOGW(TAGS, "Could not read data from sensor");
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void sensor_dht_start_task(void)
{
    xTaskCreate(dht_task, "dht_task", 2048, NULL, 5, NULL);
}
