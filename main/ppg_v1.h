#pragma once

#include <stdint.h>

void PPG_V1_Init(void);
void PPG_V1_Process(uint16_t ir_raw, uint16_t red_raw);
