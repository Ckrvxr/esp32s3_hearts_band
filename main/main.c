#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "ble.h"

#include "display.h"
#include "key.h"
#include "max30100.h"
#include "ppg.h"
#include "ppg_v2.h"
#include "sqi.h"

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

static void vPpgTask(void *pvParameters)
{
    PPG_Init();
    PPG_V2_Init();
    SQI_Init();
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
                for (int i = 0; i < n; i++) {
                    SQI_FeedSample(ir[i], red[i]);

                    MAX30100_AutoAdjust_FeedSample(ir[i]);
                    PPG_PushSample(ir[i], red[i]);

                    if (SQI_HasSignal()) {
                        PPG_V2_Process(ir[i], red[i]);
                    }
                }

                if (!SQI_HasSignal()) {
                    if (!xIdleStart) xIdleStart = now;
                    if ((now - xIdleStart) >= pdMS_TO_TICKS(1000)) {
                        g_max30100_state = MAX30100_STATE_IDLE;
                        xIdleStart = 0;
                    }
                } else {
                    xIdleStart = 0;
                }

                MAX30100_AutoAdjust_Run(0, 0, now_ms);
                break;
            }

            case MAX30100_STATE_IDLE:
            {
                uint8_t n = MAX30100_ReadFifo(ir, red);
                for (int i = 0; i < n; i++) {
                    SQI_FeedSample(ir[i], red[i]);
                    PPG_PushSample(ir[i], red[i]);
                }

                if (SQI_HasSignal()) {
                    g_max30100_state = MAX30100_STATE_NORMAL;
                    break;
                }

                if (!xIdleStart) xIdleStart = now;
                if ((now - xIdleStart) >= pdMS_TO_TICKS(3000)) {
                    MAX30100_Sleep();
                    Display_Sleep(true);
                    g_max30100_state = MAX30100_STATE_SLEEPING;
                    xIdleStart = 0;
                }
                break;
            }

            case MAX30100_STATE_SLEEPING:
            {
                if (!xIdleStart) xIdleStart = now;
                if ((now - xIdleStart) >= pdMS_TO_TICKS(MAX30100_WAKE_INTERVAL_MS)) {
                    MAX30100_Wake();
                    vTaskDelay(pdMS_TO_TICKS(50));
                    xLastWakeTime = xTaskGetTickCount();

                    uint8_t n = MAX30100_ReadFifo(ir, red);
                    for (int i = 0; i < n; i++) {
                        SQI_FeedSample(ir[i], red[i]);
                    }

                    if (SQI_HasSignal()) {
                        Display_Sleep(false);
                        g_max30100_state = MAX30100_STATE_NORMAL;
                    } else {
                        MAX30100_Sleep();
                    }
                    xIdleStart = 0;
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
    xTaskCreatePinnedToCore(vPpgTask, "PpgTask", 4096, NULL, 1, NULL, tskNO_AFFINITY);

    Ble_Driver_Init();
}
