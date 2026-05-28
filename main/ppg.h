#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define PPG_SAMPLE_BUF   600

extern uint16_t ppg_ir_buf[PPG_SAMPLE_BUF];
extern uint16_t ppg_red_buf[PPG_SAMPLE_BUF];
extern uint16_t ppg_buf_head;
extern uint16_t ppg_buf_count;
extern SemaphoreHandle_t ppg_mutex;

extern uint8_t  ppg_beat_buf[PPG_SAMPLE_BUF];
extern float    ppg_proc_buf[PPG_SAMPLE_BUF];
extern volatile uint8_t ppg_hr;
extern volatile uint8_t ppg_spo2;

void PPG_Init(void);
void PPG_PushSample(uint16_t ir, uint16_t red);
