#pragma once

#include <stdint.h>

#define PPG_SPECTRUM_BINS  103

extern float   ppg_spectrum_dB[PPG_SPECTRUM_BINS];
extern uint8_t ppg_peak_bin;
extern float   ppg_peak_freq;

void PPG_V2_Init(void);
void PPG_V2_Process(uint16_t ir_raw, uint16_t red_raw);
