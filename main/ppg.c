#include <string.h>
#include "ppg.h"

uint16_t ppg_ir_buf[PPG_SAMPLE_BUF];
uint16_t ppg_red_buf[PPG_SAMPLE_BUF];
uint16_t ppg_buf_head = 0;
uint16_t ppg_buf_count = 0;
SemaphoreHandle_t ppg_mutex = NULL;

uint8_t  ppg_beat_buf[PPG_SAMPLE_BUF];
float    ppg_proc_buf[PPG_SAMPLE_BUF];
volatile uint8_t ppg_hr = 0;
volatile uint8_t ppg_spo2 = 0;

void PPG_Init(void)
{
    ppg_mutex = xSemaphoreCreateMutex();
}

void PPG_PushSample(uint16_t ir, uint16_t red)
{
    if (ppg_mutex == NULL) return;
    xSemaphoreTake(ppg_mutex, portMAX_DELAY);
    ppg_ir_buf[ppg_buf_head] = ir;
    ppg_red_buf[ppg_buf_head] = red;
    ppg_beat_buf[ppg_buf_head] = 0;
    ppg_proc_buf[ppg_buf_head] = 0.0f;
    ppg_buf_head = (ppg_buf_head + 1) % PPG_SAMPLE_BUF;
    if (ppg_buf_count < PPG_SAMPLE_BUF) ppg_buf_count++;
    xSemaphoreGive(ppg_mutex);
}
