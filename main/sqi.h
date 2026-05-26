#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    SQI_INVALID   = 0,
    SQI_POOR      = 1,
    SQI_FAIR      = 2,
    SQI_GOOD      = 3,
    SQI_EXCELLENT = 4,
} sqi_level_t;

void SQI_Init(void);
void SQI_FeedSample(uint16_t ir_raw, uint16_t red_raw);
sqi_level_t SQI_GetLevel(void);
float SQI_GetScore(void);
bool SQI_HasSignal(void);
