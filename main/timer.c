#include "esp_timer.h"
#include "esp_log.h"

#include "timer.h"

static const char *TAG = "TIMER";

static int64_t g_timer_end_us[TIMER_COUNT];
static uint32_t g_timer_total_secs[TIMER_COUNT];
static bool g_timer_just_expired[TIMER_COUNT];

void Timer_Init(void)
{
    for (int i = 0; i < TIMER_COUNT; i++) {
        g_timer_end_us[i] = 0;
        g_timer_total_secs[i] = 0;
        g_timer_just_expired[i] = false;
    }
    ESP_LOGI(TAG, "Timer driver initialized");
}

void Timer_Set(TimerType_t type, uint32_t minutes)
{
    if (type >= TIMER_COUNT) return;
    g_timer_end_us[type] = esp_timer_get_time() + (int64_t)minutes * 60 * 1000000;
    g_timer_total_secs[type] = minutes * 60;
    g_timer_just_expired[type] = false;
    ESP_LOGI(TAG, "%s timer set: %u min",
             type == TIMER_DRINK ? "drink" : "medicine", minutes);
}

void Timer_Cancel(TimerType_t type)
{
    if (type >= TIMER_COUNT) return;
    g_timer_end_us[type] = 0;
    g_timer_total_secs[type] = 0;
    g_timer_just_expired[type] = false;
    ESP_LOGI(TAG, "%s timer cancelled",
             type == TIMER_DRINK ? "drink" : "medicine");
}

bool Timer_IsActive(TimerType_t type)
{
    if (type >= TIMER_COUNT) return false;
    if (g_timer_end_us[type] == 0) return false;
    return esp_timer_get_time() < g_timer_end_us[type];
}

void Timer_CheckExpiry(void)
{
    int64_t now_us = esp_timer_get_time();
    for (int i = 0; i < TIMER_COUNT; i++) {
        if (g_timer_end_us[i] > 0 && now_us >= g_timer_end_us[i]) {
            ESP_LOGI(TAG, "%s timer expired",
                     i == TIMER_DRINK ? "drink" : "medicine");
            g_timer_end_us[i] = 0;
            g_timer_just_expired[i] = true;
        }
    }
}

uint32_t Timer_GetRemainingSecs(TimerType_t type)
{
    if (type >= TIMER_COUNT) return 0;
    if (g_timer_end_us[type] == 0) return 0;
    int64_t remain_us = g_timer_end_us[type] - esp_timer_get_time();
    if (remain_us <= 0) return 0;
    return (uint32_t)(remain_us / 1000000);
}

uint32_t Timer_GetTotalSecs(TimerType_t type)
{
    if (type >= TIMER_COUNT) return 0;
    return g_timer_total_secs[type];
}

bool Timer_HasJustExpired(TimerType_t type)
{
    if (type >= TIMER_COUNT) return false;
    return g_timer_just_expired[type];
}

void Timer_ClearExpiryFlag(TimerType_t type)
{
    if (type >= TIMER_COUNT) return;
    g_timer_just_expired[type] = false;
}
