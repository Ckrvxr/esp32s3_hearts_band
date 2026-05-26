#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "display.h"
#include "key.h"
#include "max30100.h"

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
            Key_Event_Handler(evt, key_id);
        }
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(20));
    }
}

static void vMax30100Task(void *pvParameters)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();
    TickType_t xIdleStart = 0;
    uint16_t ir[MAX30100_FIFO_DEPTH];
    uint16_t red[MAX30100_FIFO_DEPTH];

    MAX30100_AutoAdjust_Init();

    while (1) {
        TickType_t now = xTaskGetTickCount();
        uint32_t now_ms = now * portTICK_PERIOD_MS;

        switch (g_max30100_state) {
            case MAX30100_STATE_NORMAL:
            {
                uint8_t n = MAX30100_ReadFifo(ir, red);
                if (n > 0) {
                    uint8_t no_signal = 1;
                    for (int i = 0; i < n; i++) {
                        MAX30100_AutoAdjust_FeedSample(ir[i]);
                        if (ir[i] >= MAX30100_IDLE_THRESHOLD_IR || red[i] >= MAX30100_IDLE_THRESHOLD_RED) {
                            no_signal = 0;
                        }
                        ESP_LOGI("MAX30100", "IR=%5u  RED=%5u", ir[i], red[i]);
                    }
                    if (no_signal) {
                        xIdleStart = now;
                        g_max30100_state = MAX30100_STATE_IDLE;
                    }
                }

                MAX30100_AutoAdjust_Run(0, 0, now_ms);
                break;
            }

            case MAX30100_STATE_IDLE:
            {
                uint8_t n = MAX30100_ReadFifo(ir, red);
                if (n > 0) {
                    uint8_t no_signal = 1;
                    for (int i = 0; i < n; i++) {
                        if (ir[i] >= MAX30100_IDLE_THRESHOLD_IR || red[i] >= MAX30100_IDLE_THRESHOLD_RED) {
                            no_signal = 0;
                        }
                        ESP_LOGI("MAX30100", "IR=%5u  RED=%5u", ir[i], red[i]);
                    }
                    if (!no_signal) {
                        g_max30100_state = MAX30100_STATE_NORMAL;
                    } else if ((now - xIdleStart) >= pdMS_TO_TICKS(MAX30100_IDLE_TIMEOUT_MS)) {
                        MAX30100_Sleep();
                        xIdleStart = now;
                    }
                }
                break;
            }

            case MAX30100_STATE_SLEEPING:
            {
                if ((now - xIdleStart) >= pdMS_TO_TICKS(MAX30100_WAKE_INTERVAL_MS)) {
                    MAX30100_Wake();
                    vTaskDelay(pdMS_TO_TICKS(50));
                    xLastWakeTime = xTaskGetTickCount();

                    uint8_t n = MAX30100_ReadFifo(ir, red);
                    if (n > 0) {
                        uint8_t signal_back = 0;
                        for (int i = 0; i < n; i++) {
                            if (ir[i] >= MAX30100_IDLE_THRESHOLD_IR || red[i] >= MAX30100_IDLE_THRESHOLD_RED) {
                                signal_back = 1;
                            }
                            ESP_LOGI("MAX30100", "IR=%5u  RED=%5u", ir[i], red[i]);
                        }
                        if (signal_back) {
                            g_max30100_state = MAX30100_STATE_NORMAL;
                        } else {
                            MAX30100_Sleep();
                        }
                    } else {
                        MAX30100_Sleep();
                    }
                    xIdleStart = xTaskGetTickCount();
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
    MAX30100_Init();
    xTaskCreatePinnedToCore(MainTask, "MainTask", 4096, NULL, 1, NULL, tskNO_AFFINITY);
    xTaskCreatePinnedToCore(vDisplayTask, "DisplayTask", 6144, NULL, 2, NULL, tskNO_AFFINITY);
    xTaskCreatePinnedToCore(vKeyTask, "KeyTask", 2048, NULL, 1, NULL, tskNO_AFFINITY);
    xTaskCreatePinnedToCore(vMax30100Task, "Max30100Task", 4096, NULL, 1, NULL, tskNO_AFFINITY);
}
