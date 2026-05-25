#pragma once

#include <stdint.h>

typedef enum {
    STATE_MAIN_SCREEN,
} DisplayState_t;

extern volatile DisplayState_t currentState;
extern volatile uint8_t menu_index;
extern volatile uint8_t slect_index;

void Display_Init(void);
void Display_Refresh(void);