#pragma once

#include <stdint.h>
#include <stdbool.h>

#define PPG_SAMPLE_BUF   600

void   PPG_Init(void);
void   PPG_PushSample(uint16_t ir, uint16_t red);
void   PPG_WriteProcessedSample(float proc_val, uint8_t beat_flag);
void   PPG_SetHR(uint8_t hr);
void   PPG_SetSpO2(uint8_t spo2);

uint8_t      PPG_GetHR(void);
uint8_t      PPG_GetSpO2(void);
void         PPG_Lock(void);
void         PPG_Unlock(void);
uint16_t     PPG_GetHead(void);
uint16_t     PPG_GetCount(void);
uint16_t     PPG_ReadIR(uint16_t idx);
uint16_t     PPG_ReadRED(uint16_t idx);
uint8_t      PPG_ReadBeat(uint16_t idx);
float        PPG_ReadProc(uint16_t idx);
