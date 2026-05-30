#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    TIMER_DRINK,
    TIMER_MEDICINE,
    TIMER_COUNT,
} TimerType_t;

typedef struct {
    TimerType_t type;
    bool is_set;
    uint32_t minutes;
} TimerConfirmEntry_t;

void Timer_Init(void);
void Timer_Set(TimerType_t type, uint32_t minutes);
void Timer_Cancel(TimerType_t type);
bool Timer_IsActive(TimerType_t type);
void Timer_CheckExpiry(void);
uint32_t Timer_GetRemainingSecs(TimerType_t type);
uint32_t Timer_GetTotalSecs(TimerType_t type);
bool Timer_HasJustExpired(TimerType_t type);
void Timer_ClearExpiryFlag(TimerType_t type);
void Timer_PushConfirm(TimerType_t type, bool is_set, uint32_t minutes);
bool Timer_PopConfirm(TimerConfirmEntry_t *out);
bool Timer_PeekConfirm(TimerConfirmEntry_t *out);
