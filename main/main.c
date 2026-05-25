#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "display.h"
#include "key.h"

static const char *TAG = "RTOS";

static void MainTask(void *pvParameters)
{
    while (1) {
        // ESP_LOGI(TAG, "System running.");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

static void vDisplayTask(void *pvParameters)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();
    while (1) {
        Display_Refresh();
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(20));
    }
}

static void vKeyTask(void *pvParameters)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();
    uint8_t key_id;

    while (1) {
        KeyEvent_t evt = Key_Scan(&key_id);
        if (evt != KEY_EVENT_NONE) {
            switch (key_id) {
                case KEY_IDX_UP:
                    switch (evt) {
                        case KEY_EVENT_CLICK:
                            ESP_LOGI(TAG, "UP CLICK");
                            break;
                        case KEY_EVENT_DOUBLE_CLICK:
                            ESP_LOGI(TAG, "UP DOUBLE_CLICK");
                            break;
                        case KEY_EVENT_LONG_PRESS:
                            ESP_LOGI(TAG, "UP LONG_PRESS");
                            break;
                        default: break;
                    }
                    break;
                case KEY_IDX_DOWN:
                    switch (evt) {
                        case KEY_EVENT_CLICK:
                            ESP_LOGI(TAG, "DOWN CLICK");
                            break;
                        case KEY_EVENT_DOUBLE_CLICK:
                            ESP_LOGI(TAG, "DOWN DOUBLE_CLICK");
                            break;
                        case KEY_EVENT_LONG_PRESS:
                            ESP_LOGI(TAG, "DOWN LONG_PRESS");
                            break;
                        default: break;
                    }
                    break;
                case KEY_IDX_CONFIRM:
                    switch (evt) {
                        case KEY_EVENT_CLICK:
                            ESP_LOGI(TAG, "CONFIRM CLICK");
                            break;
                        case KEY_EVENT_DOUBLE_CLICK:
                            ESP_LOGI(TAG, "CONFIRM DOUBLE_CLICK");
                            break;
                        case KEY_EVENT_LONG_PRESS:
                            ESP_LOGI(TAG, "CONFIRM LONG_PRESS");
                            break;
                        default: break;
                    }
                    break;
                case KEY_IDX_CANCEL:
                    switch (evt) {
                        case KEY_EVENT_CLICK:
                            ESP_LOGI(TAG, "CANCEL CLICK");
                            break;
                        case KEY_EVENT_DOUBLE_CLICK:
                            ESP_LOGI(TAG, "CANCEL DOUBLE_CLICK");
                            break;
                        case KEY_EVENT_LONG_PRESS:
                            ESP_LOGI(TAG, "CANCEL LONG_PRESS");
                            break;
                        default: break;
                    }
                    break;
            }
        }
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(20));
    }
}

void app_main(void)
{
    Display_Init();
    Key_Init();
    xTaskCreatePinnedToCore(MainTask, "MainTask", 4096, NULL, 1, NULL, tskNO_AFFINITY);
    xTaskCreatePinnedToCore(vDisplayTask, "DisplayTask", 6144, NULL, 2, NULL, tskNO_AFFINITY);
    xTaskCreatePinnedToCore(vKeyTask, "KeyTask", 2048, NULL, 1, NULL, tskNO_AFFINITY);
}
