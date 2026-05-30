#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ble.h"
#include "driver/uart.h"

#include "display.h"
#include "key.h"
#include "max30100.h"
#include "ppg.h"
#include "ppg_v3.h"
#include "timer.h"

static void MainTask(void *pvParameters)
{
    while (1) {
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
    PPG_V3_Init();
    TickType_t xLastWakeTime = xTaskGetTickCount();
    TickType_t xIdleStart = 0;
    uint16_t ir[MAX30100_FIFO_DEPTH];
    uint16_t red[MAX30100_FIFO_DEPTH];
    static float dc_ir = 0, dc_red = 0;

    MAX30100_AutoAdjust_Init();

    while (1) {
        TickType_t now = xTaskGetTickCount();
        uint32_t now_ms = now * portTICK_PERIOD_MS;

        switch (g_max30100_state) {
            case MAX30100_STATE_NORMAL:
            {
                uint8_t n = MAX30100_ReadFifo(ir, red);
                for (int i = 0; i < n; i++) {
                    dc_ir  = 0.999f * dc_ir  + 0.001f * ir[i];
                    dc_red = 0.999f * dc_red + 0.001f * red[i];
                    MAX30100_AutoAdjust_FeedSample(ir[i]);
                    PPG_PushSample(ir[i], red[i]);
                    PPG_V3_Process(ir[i], red[i]);
                }

                if (!PPG_V3_HasContact()) {
                    if (!xIdleStart) xIdleStart = now;
                    if ((now - xIdleStart) >= pdMS_TO_TICKS(1000)) {
                        g_max30100_state = MAX30100_STATE_IDLE;
                        xIdleStart = 0;
                    }
                } else {
                    xIdleStart = 0;
                }

                MAX30100_AutoAdjust_Run(dc_ir, dc_red, now_ms);
                break;
            }

            case MAX30100_STATE_IDLE:
            {
                uint8_t n = MAX30100_ReadFifo(ir, red);
                for (int i = 0; i < n; i++) {
                    dc_ir  = 0.999f * dc_ir  + 0.001f * ir[i];
                    dc_red = 0.999f * dc_red + 0.001f * red[i];
                }

                PPG_PushSample(0, 0);
                PPG_V3_Process(0, 0);

                if (dc_ir > 2000.0f) {
                    g_max30100_state = MAX30100_STATE_NORMAL;
                    break;
                }

                if (!xIdleStart) xIdleStart = now;
                if ((now - xIdleStart) >= pdMS_TO_TICKS(3000)) {
                    MAX30100_Sleep();
                    g_max30100_state = MAX30100_STATE_SLEEPING;
                    xIdleStart = 0;
                }
                break;
            }

            case MAX30100_STATE_SLEEPING:
            {
                PPG_PushSample(0, 0);
                PPG_V3_Process(0, 0);

                if (!xIdleStart) xIdleStart = now;
                if ((now - xIdleStart) >= pdMS_TO_TICKS(MAX30100_WAKE_INTERVAL_MS)) {
                    MAX30100_Wake();
                    MAX30100_WriteReg(MAX30100_REG_LED_CONFIG, (0x08 << 4) | 0x01);
                    vTaskDelay(pdMS_TO_TICKS(10));

                    uint8_t n = MAX30100_ReadFifo(ir, red);
                    if (n > 5) n = 5;

                    uint32_t ir_sum = 0;
                    for (int i = 0; i < n; i++) ir_sum += ir[i];
                    float ir_avg = (float)ir_sum / (n > 0 ? n : 1);

                    if (ir_avg > 2000.0f) {
                        g_max30100_state = MAX30100_STATE_NORMAL;
                    } else {
                        MAX30100_Sleep();
                    }
                    xIdleStart = 0;
                }
                break;
            }
        }

        // 1-second interval housekeeping (50Hz × 20ms = 1000ms)
        static uint32_t report_counter = 0;
        report_counter++;
        if (report_counter >= 50) {
            report_counter = 0;
            if (Ble_Driver_IsConnected()) {
                Ble_Driver_ReportHealth(PPG_GetHR(), PPG_GetSpO2());
            }
        }

        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(20));
    }
}

static void vTimerTask(void *pvParameters)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();
    TickType_t timer_set_start = 0;
    uint8_t debug_startup = 0;
    while (1) {
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(1000));

        if (debug_startup < 10) {
            debug_startup++;
            if (debug_startup == 10) {
                Timer_Set(TIMER_DRINK, 1);
                currentState = STATE_TIMER_SET;
            }
        }

        Timer_CheckExpiry();

        if (Timer_HasJustExpired(TIMER_DRINK) || Timer_HasJustExpired(TIMER_MEDICINE)) {
            currentState = STATE_TIMER_DONE;
        }

        if (currentState == STATE_TIMER_SET) {
            if (timer_set_start == 0)
                timer_set_start = xTaskGetTickCount();
            else if ((xTaskGetTickCount() - timer_set_start) >= pdMS_TO_TICKS(5000)) {
                timer_set_start = 0;
                TimerConfirmEntry_t dummy;
                Timer_PopConfirm(&dummy);
                if (!Timer_PeekConfirm(&dummy))
                    currentState = STATE_TIMER_STATUS;
            }
        } else {
            timer_set_start = 0;
        }
    }
}

// ── Static task buffers ────────────────────────────────────────────
static StackType_t main_task_stack[4096 / sizeof(StackType_t)];
static StaticTask_t main_task_tcb;
static StackType_t display_task_stack[6144 / sizeof(StackType_t)];
static StaticTask_t display_task_tcb;
static StackType_t key_task_stack[2048 / sizeof(StackType_t)];
static StaticTask_t key_task_tcb;
static StackType_t ppg_task_stack[4096 / sizeof(StackType_t)];
static StaticTask_t ppg_task_tcb;
static StackType_t timer_task_stack[2048 / sizeof(StackType_t)];
static StaticTask_t timer_task_tcb;

void app_main(void)
{
    Display_Init();
    Key_Init();
    MAX30100_Init();
    Timer_Init();

    uart_config_t uart_cfg = {
        .baud_rate = 921600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
    uart_param_config(UART_NUM_1, &uart_cfg);
    uart_set_pin(UART_NUM_1, 21, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_NUM_1, 256, 0, 0, NULL, 0);

    xTaskCreateStaticPinnedToCore(MainTask, "MainTask", 4096, NULL, 1,
        main_task_stack, &main_task_tcb, tskNO_AFFINITY);
    xTaskCreateStaticPinnedToCore(vDisplayTask, "DisplayTask", 6144, NULL, 2,
        display_task_stack, &display_task_tcb, tskNO_AFFINITY);
    xTaskCreateStaticPinnedToCore(vKeyTask, "KeyTask", 2048, NULL, 1,
        key_task_stack, &key_task_tcb, tskNO_AFFINITY);
    xTaskCreateStaticPinnedToCore(vPpgTask, "PpgTask", 4096, NULL, 1,
        ppg_task_stack, &ppg_task_tcb, tskNO_AFFINITY);
    xTaskCreateStaticPinnedToCore(vTimerTask, "TimerTask", 2048, NULL, 1,
        timer_task_stack, &timer_task_tcb, tskNO_AFFINITY);

    Ble_Driver_Init();
}
