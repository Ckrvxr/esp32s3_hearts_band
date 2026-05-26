#include <math.h>
#include "ppg.h"
#include "ppg_v1.h"

// ============================================================
// DCRemover state (HPF, α = 0.95)
// ============================================================
static float dc_w_ir;
static float dc_w_red;
static float dc_ir_slow;
static float dc_red_slow;

// ============================================================
// Butterworth 4th order LPF state (two biquad sections, fc=5Hz)
// ============================================================
static float bq1_x1_ir, bq1_x2_ir;
static float bq1_y1_ir, bq1_y2_ir;
static float bq2_x1_ir, bq2_x2_ir;
static float bq2_y1_ir, bq2_y2_ir;

// ============================================================
// BeatDetector state (5-state adaptive threshold)
// ============================================================
#define BD_INIT         0
#define BD_WAITING      1
#define BD_FOLLOWING    2
#define BD_MAYBE        3
#define BD_MASKING      4

#define BD_N_INIT       200
#define BD_MASK_SAMPLES 20
#define BD_TIMEOUT_SAMPLES 200
#define BD_THRESH_FALLOFF 0.3f
#define BD_DELTA        30.0f
#define BD_GAMMA        0.99f
#define BD_THRESH_MIN   20.0f
#define BD_THRESH_MAX   800.0f

static uint8_t  bd_state;
static float    bd_threshold;
static float    bd_pmax;
static uint32_t bd_sample_count;
static uint32_t bd_last_beat_sample;
static float    bd_ema_interval;

// ============================================================
// SpO₂ state
// ============================================================
static float    spo2_sum_ir_sq;
static float    spo2_sum_red_sq;
static uint32_t spo2_samples;
static uint8_t  spo2_beats;
static uint32_t spo2_last_beat_sample;

static const uint8_t spo2_knots_k[9] = {0, 4, 10, 16, 22, 28, 34, 38, 42};
static const uint8_t spo2_knots_v[9] = {100, 99, 98, 97, 96, 95, 94, 93, 93};

static uint8_t spo2_lookup(float R_log)
{
    int k;
    if (R_log > 66.0f) {
        k = (int)(R_log - 66.0f);
    } else if (R_log > 50.0f) {
        k = (int)(R_log - 50.0f);
    } else {
        k = 0;
    }
    if (k < 0) k = 0;
    if (k > 42) k = 42;

    for (int i = 0; i < 8; i++) {
        if (k >= spo2_knots_k[i] && k <= spo2_knots_k[i + 1]) {
            int dk = spo2_knots_k[i + 1] - spo2_knots_k[i];
            if (dk == 0) return spo2_knots_v[i];
            float t = (float)(k - spo2_knots_k[i]) / (float)dk;
            return (uint8_t)(spo2_knots_v[i] - t * (spo2_knots_v[i] - spo2_knots_v[i + 1]) + 0.5f);
        }
    }
    return spo2_knots_v[8];
}

// ============================================================
// Public API
// ============================================================
void PPG_V1_Init(void)
{
    dc_w_ir = 0.0f;
    dc_w_red = 0.0f;
    dc_ir_slow = 0.0f;
    dc_red_slow = 0.0f;

    bq1_x1_ir = 0.0f; bq1_x2_ir = 0.0f;
    bq1_y1_ir = 0.0f; bq1_y2_ir = 0.0f;
    bq2_x1_ir = 0.0f; bq2_x2_ir = 0.0f;
    bq2_y1_ir = 0.0f; bq2_y2_ir = 0.0f;

    bd_state = BD_INIT;
    bd_threshold = BD_THRESH_MIN;
    bd_pmax = 0.0f;
    bd_sample_count = 0;
    bd_last_beat_sample = 0;
    bd_ema_interval = 0.0f;

    spo2_sum_ir_sq = 0.0f;
    spo2_sum_red_sq = 0.0f;
    spo2_samples = 0;
    spo2_beats = 0;
    spo2_last_beat_sample = 0;

    ppg_hr = 0;
    ppg_spo2 = 99;
}

void PPG_V1_Process(uint16_t ir_raw, uint16_t red_raw)
{
    // --------------------------------------------------------
    // 1. DCRemover
    // --------------------------------------------------------
    float x_ir = (float)ir_raw;
    float w_ir = x_ir + 0.95f * dc_w_ir;
    float ac_ir = w_ir - dc_w_ir;
    dc_w_ir = w_ir;

    float x_red = (float)red_raw;
    float w_red = x_red + 0.95f * dc_w_red;
    float ac_red = w_red - dc_w_red;
    dc_w_red = w_red;

    dc_ir_slow = 0.999f * dc_ir_slow + 0.001f * x_ir;
    dc_red_slow = 0.999f * dc_red_slow + 0.001f * x_red;

    // --------------------------------------------------------
    // 2. Butterworth LPF (4th order, fc = 5 Hz)
    // --------------------------------------------------------
    float y1 = 0.0205f * (ac_ir + 2.0f * bq1_x1_ir + bq1_x2_ir)
               + 1.5928f * bq1_y1_ir - 0.6712f * bq1_y2_ir;
    bq1_x2_ir = bq1_x1_ir;
    bq1_x1_ir = ac_ir;
    bq1_y2_ir = bq1_y1_ir;
    bq1_y1_ir = y1;

    float y2 = (y1 + 2.0f * bq2_x1_ir + bq2_x2_ir)
               + 1.4409f * bq2_y1_ir - 0.5421f * bq2_y2_ir;
    bq2_x2_ir = bq2_x1_ir;
    bq2_x1_ir = y1;
    bq2_y2_ir = bq2_y1_ir;
    bq2_y1_ir = y2;

    float p = -y2;
    bd_sample_count++;

    // --------------------------------------------------------
    // 3. BeatDetector
    // --------------------------------------------------------
    bool beat = false;

    switch (bd_state) {
        case BD_INIT:
            bd_threshold = BD_THRESH_MIN;
            bd_pmax = 0.0f;
            if (bd_sample_count >= BD_N_INIT) {
                bd_state = BD_WAITING;
            }
            break;

        case BD_WAITING:
            if (p > bd_threshold) {
                bd_state = BD_FOLLOWING;
                bd_pmax = p;
            } else {
                if (bd_ema_interval < 1.0f) {
                    bd_threshold *= BD_GAMMA;
                } else {
                    float delta = bd_pmax * (1.0f - BD_THRESH_FALLOFF) / bd_ema_interval;
                    bd_threshold -= delta;
                }
                if (bd_threshold < BD_THRESH_MIN) bd_threshold = BD_THRESH_MIN;
            }
            break;

        case BD_FOLLOWING:
            if (p > bd_pmax) bd_pmax = p;
            if (p < bd_threshold) {
                bd_state = BD_MAYBE;
            }
            break;

        case BD_MAYBE:
            if (p + BD_DELTA < bd_threshold) {
                beat = true;
                bd_state = BD_MASKING;

                uint32_t interval = bd_sample_count - bd_last_beat_sample;
                bd_last_beat_sample = bd_sample_count;

                if (bd_ema_interval < 1.0f) {
                    bd_ema_interval = (float)interval;
                } else {
                    bd_ema_interval = 0.6f * (float)interval + 0.4f * bd_ema_interval;
                }

                if (bd_ema_interval > 1.0f) {
                    ppg_hr = (uint8_t)(6000.0f / bd_ema_interval + 0.5f);
                }

                bd_threshold = BD_THRESH_FALLOFF * bd_pmax;
                if (bd_threshold < BD_THRESH_MIN) bd_threshold = BD_THRESH_MIN;

            } else if (p > bd_threshold) {
                bd_state = BD_FOLLOWING;
            }
            break;

        case BD_MASKING:
            if (bd_sample_count - bd_last_beat_sample >= BD_MASK_SAMPLES) {
                bd_state = BD_WAITING;
            }
            break;
    }

    // --------------------------------------------------------
    // 4. SpO₂
    // --------------------------------------------------------
    spo2_sum_ir_sq += ac_ir * ac_ir;
    spo2_sum_red_sq += ac_red * ac_red;
    spo2_samples++;

    if (beat) {
        spo2_beats++;
        spo2_last_beat_sample = bd_sample_count;

        if (spo2_beats >= 3 && spo2_samples > 0) {
            float avg_ir_sq = spo2_sum_ir_sq / (float)spo2_samples;
            float avg_red_sq = spo2_sum_red_sq / (float)spo2_samples;

            if (avg_ir_sq > 1.0f && avg_red_sq > 1.0f && dc_ir_slow > 100.0f && dc_red_slow > 100.0f) {
                float R_log = 100.0f * logf(avg_red_sq) / logf(avg_ir_sq);
                ppg_spo2 = spo2_lookup(R_log);
            }

            spo2_sum_ir_sq = 0.0f;
            spo2_sum_red_sq = 0.0f;
            spo2_samples = 0;
            spo2_beats = 0;
        }
    }

    if (bd_sample_count - spo2_last_beat_sample > BD_TIMEOUT_SAMPLES && spo2_samples > 0) {
        spo2_sum_ir_sq = 0.0f;
        spo2_sum_red_sq = 0.0f;
        spo2_samples = 0;
        spo2_beats = 0;
    }

    // --------------------------------------------------------
    // 5. Store processed data
    // --------------------------------------------------------
    uint16_t idx = (ppg_buf_head == 0) ? (PPG_SAMPLE_BUF - 1) : (ppg_buf_head - 1);

    xSemaphoreTake(ppg_mutex, portMAX_DELAY);
    ppg_proc_buf[idx] = p;
    if (beat) {
        ppg_beat_buf[idx] = 1;
    }
    xSemaphoreGive(ppg_mutex);
}
