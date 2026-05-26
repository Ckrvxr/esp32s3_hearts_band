#include "sqi.h"
#include <string.h>

#define SQI_WINDOW      50

static uint32_t  acc_ir_sum;
static uint32_t  acc_red_sum;
static uint16_t  acc_ir_min;
static uint16_t  acc_ir_max;
static uint16_t  acc_sat_count;
static uint16_t  acc_samples;

static volatile sqi_level_t current_level;
static float     current_score;
static bool      window_ready;

void SQI_Init(void)
{
    acc_samples = 0;
    acc_ir_sum  = 0;
    acc_red_sum = 0;
    acc_ir_min  = 0xFFFF;
    acc_ir_max  = 0;
    acc_sat_count = 0;
    current_level  = SQI_INVALID;
    current_score  = 0.0f;
    window_ready   = false;
}

void SQI_FeedSample(uint16_t ir_raw, uint16_t red_raw)
{
    acc_ir_sum  += ir_raw;
    acc_red_sum += red_raw;
    if (ir_raw < acc_ir_min) acc_ir_min = ir_raw;
    if (ir_raw > acc_ir_max) acc_ir_max = ir_raw;
    if (ir_raw > 65000 || red_raw > 65000) acc_sat_count++;
    acc_samples++;

    if (acc_samples < SQI_WINDOW) return;

    float dc_ir   = (float)acc_ir_sum / (float)acc_samples;
    float ac_ir_pp = (float)(acc_ir_max - acc_ir_min);

    float contact = 0.0f;
    if (dc_ir > 2000.0f) {
        contact = (dc_ir - 2000.0f) / 58000.0f;
        if (contact > 1.0f) contact = 1.0f;
    }

    float pulse = 0.0f;
    if (ac_ir_pp > 30.0f) {
        pulse = ac_ir_pp / 2000.0f;
        if (pulse > 1.0f) pulse = 1.0f;
    }

    float motion = 1.0f;
    if (ac_ir_pp > 1500.0f && pulse < 0.4f) {
        motion = 1000.0f / (ac_ir_pp + 1.0f);
        if (motion > 1.0f) motion = 1.0f;
    }

    float sat_ratio = (float)acc_sat_count / (float)SQI_WINDOW;
    float sat_ok = (sat_ratio > 0.2f) ? 0.0f : 1.0f;

    current_score = contact * 0.30f + pulse * 0.35f + motion * 0.25f + sat_ok * 0.10f;

    if (current_score < 0.15f || contact < 0.05f) {
        current_level = SQI_INVALID;
    } else if (current_score < 0.35f || pulse < 0.08f) {
        current_level = SQI_POOR;
    } else if (current_score < 0.55f) {
        current_level = SQI_FAIR;
    } else if (current_score < 0.80f) {
        current_level = SQI_GOOD;
    } else {
        current_level = SQI_EXCELLENT;
    }

    window_ready = true;

    acc_samples  = 0;
    acc_ir_sum   = 0;
    acc_red_sum  = 0;
    acc_ir_min   = 0xFFFF;
    acc_ir_max   = 0;
    acc_sat_count = 0;
}

sqi_level_t SQI_GetLevel(void)
{
    if (!window_ready) return SQI_POOR;
    return current_level;
}

float SQI_GetScore(void)
{
    return current_score;
}

bool SQI_HasSignal(void)
{
    return current_level >= SQI_FAIR;
}
