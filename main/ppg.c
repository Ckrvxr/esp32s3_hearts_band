#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "ppg.h"

static uint16_t ppg_ir_buf[PPG_SAMPLE_BUF];
static uint16_t ppg_red_buf[PPG_SAMPLE_BUF];
static uint16_t ppg_buf_head = 0;
static uint16_t ppg_buf_count = 0;
static SemaphoreHandle_t ppg_mutex = NULL;

static uint8_t  ppg_beat_buf[PPG_SAMPLE_BUF];
static float    ppg_proc_buf[PPG_SAMPLE_BUF];
static volatile uint8_t ppg_hr = 0;
static volatile uint8_t ppg_spo2 = 0;

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

void PPG_WriteProcessedSample(float proc_val, uint8_t beat_flag)
{
    uint16_t idx = (ppg_buf_head == 0) ? (PPG_SAMPLE_BUF - 1) : (ppg_buf_head - 1);
    ppg_proc_buf[idx] = proc_val;
    ppg_beat_buf[idx] = beat_flag;
}

void PPG_SetHR(uint8_t hr)
{
    ppg_hr = hr;
}

void PPG_SetSpO2(uint8_t spo2)
{
    ppg_spo2 = spo2;
}

uint8_t PPG_GetHR(void)
{
    return ppg_hr;
}

uint8_t PPG_GetSpO2(void)
{
    return ppg_spo2;
}

void PPG_Lock(void)
{
    if (ppg_mutex) xSemaphoreTake(ppg_mutex, portMAX_DELAY);
}

void PPG_Unlock(void)
{
    if (ppg_mutex) xSemaphoreGive(ppg_mutex);
}

uint16_t PPG_GetHead(void)
{
    return ppg_buf_head;
}

uint16_t PPG_GetCount(void)
{
    return ppg_buf_count;
}

uint16_t PPG_ReadIR(uint16_t idx)
{
    return ppg_ir_buf[idx];
}

uint16_t PPG_ReadRED(uint16_t idx)
{
    return ppg_red_buf[idx];
}

uint8_t PPG_ReadBeat(uint16_t idx)
{
    return ppg_beat_buf[idx];
}

float PPG_ReadProc(uint16_t idx)
{
    return ppg_proc_buf[idx];
}
