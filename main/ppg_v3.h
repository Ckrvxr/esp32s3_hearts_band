#pragma once

#include <stdint.h>
#include <stdbool.h>

void PPG_V3_Init(void);
void PPG_V3_Process(uint16_t ir_raw, uint16_t red_raw);
bool PPG_V3_HasContact(void);
