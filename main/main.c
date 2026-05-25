#include "esp_log.h"

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "RTOS";

static void MainTask(void *pvParameters)
{
    while (1) {
        ESP_LOGI(TAG, "System running.");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void app_main(void)
{
    xTaskCreatePinnedToCore(MainTask, "MainTask", 4096, NULL, 1, NULL, tskNO_AFFINITY);
}
