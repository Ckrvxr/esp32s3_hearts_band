#pragma once

#include <stdint.h>

typedef enum {
    KEY_EVENT_NONE,
    KEY_EVENT_CLICK,
    KEY_EVENT_DOUBLE_CLICK,
    KEY_EVENT_LONG_PRESS,
} KeyEvent_t;

typedef enum {
    KEY_IDX_UP = 0,
    KEY_IDX_DOWN,
    KEY_IDX_CONFIRM,
    KEY_IDX_CANCEL,
} KeyIndex_t;

void Key_Init(void);
KeyEvent_t Key_Scan(uint8_t *out_key);
void Key_Event_Handler(KeyEvent_t evt, uint8_t key_id);
