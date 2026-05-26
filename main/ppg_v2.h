#pragma once

#include <stdint.h>

void PPG_V2_Init(void);
void PPG_V2_Process(uint16_t ir_raw, uint16_t red_raw);
