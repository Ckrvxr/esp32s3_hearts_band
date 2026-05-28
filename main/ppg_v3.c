#include <math.h>
#include <string.h>

#include "driver/uart.h"

#include "ppg.h"
#include "ppg_v3.h"
#include "ppg_v3_cnn_weights.h"

#define HP_ALPHA    0.04f
#define INT_K       20
#define THRESHOLD   0.5f
#define REFRACTORY  20
#define HR_SMA      0.3f

static float ir_emean, red_emean;
static float ir_hp_buf[CNN_WINDOW_SIZE];
static float red_hp_buf[CNN_WINDOW_SIZE];
static uint16_t buf_idx;
static uint32_t sample_count;

static float int_buf[INT_K];
static uint8_t int_idx;
static float int_sum;
static uint32_t last_beat_sample;
static float hr_interval_sma;
static float dc_ir_slow;
static float proc_smooth;

// SpO2 tracking
static float spo2_ac_ir_rms;
static float spo2_ac_red_rms;
static float spo2_dc_ir;
static float spo2_dc_red;
static uint32_t spo2_sample_count;

// CNN intermediate buffers
static float in[2][CNN_WINDOW_SIZE];
static float a1[8][CNN_WINDOW_SIZE];
static float a2[8][CNN_WINDOW_SIZE / 2];
static float b1[16][CNN_WINDOW_SIZE / 2];
static float b2[16][CNN_WINDOW_SIZE / 4];
static float c1[16][CNN_WINDOW_SIZE / 4];
static float c2[8][CNN_WINDOW_SIZE / 4];
static float d1[8][CNN_WINDOW_SIZE / 4 / 5];
static float d2[8][CNN_WINDOW_SIZE / 4 / 5];
static float e1[4][CNN_WINDOW_SIZE / 4 / 5];

static float cnn_forward(void)
{
    // conv1 (groups=2, k=7, pad=3)
    for (int g = 0; g < 2; g++) {
        int base = g * 4;
        for (int oc = 0; oc < 4; oc++) {
            int oo = base + oc;
            for (int pos = 0; pos < CNN_WINDOW_SIZE; pos++) {
                float sum = conv1_bias[oo];
                for (int k = 0; k < 7; k++) {
                    int ip = pos + k - 3;
                    if (ip >= 0 && ip < CNN_WINDOW_SIZE)
                        sum += conv1_weight[oo][0][k] * in[g][ip];
                }
                a1[oo][pos] = (sum > 0) ? sum : 0;
            }
        }
    }

    // pool1 (k=2)
    for (int c = 0; c < 8; c++)
        for (int p = 0; p < CNN_WINDOW_SIZE / 2; p++)
            a2[c][p] = (a1[c][2 * p] + a1[c][2 * p + 1]) * 0.5f;

    // conv2 (groups=4, k=5, pad=2)
    for (int g = 0; g < 4; g++) {
        int ic0 = g * 2;
        int base = g * 4;
        for (int oc = 0; oc < 4; oc++) {
            int oo = base + oc;
            for (int pos = 0; pos < CNN_WINDOW_SIZE / 2; pos++) {
                float sum = conv2_bias[oo];
                for (int ic = 0; ic < 2; ic++)
                    for (int k = 0; k < 5; k++) {
                        int ip = pos + k - 2;
                        if (ip >= 0 && ip < CNN_WINDOW_SIZE / 2)
                            sum += conv2_weight[oo][ic][k] * a2[ic0 + ic][ip];
                    }
                b1[oo][pos] = (sum > 0) ? sum : 0;
            }
        }
    }

    // pool2 (k=2)
    for (int c = 0; c < 16; c++)
        for (int p = 0; p < CNN_WINDOW_SIZE / 4; p++)
            b2[c][p] = (b1[c][2 * p] + b1[c][2 * p + 1]) * 0.5f;

    // dw3 (groups=16, k=3, pad=1)
    for (int c = 0; c < 16; c++) {
        float tmp[CNN_WINDOW_SIZE / 4];
        for (int pos = 0; pos < CNN_WINDOW_SIZE / 4; pos++) {
            float sum = dw3_bias[c];
            for (int k = 0; k < 3; k++) {
                int ip = pos + k - 1;
                if (ip >= 0 && ip < CNN_WINDOW_SIZE / 4)
                    sum += dw3_weight[c][0][k] * b2[c][ip];
            }
            tmp[pos] = (sum > 0) ? sum : 0;
        }
        memcpy(c1[c], tmp, sizeof(tmp));
    }

    // pw3 (k=1, 16→8)
    for (int oc = 0; oc < 8; oc++) {
        for (int pos = 0; pos < CNN_WINDOW_SIZE / 4; pos++) {
            float sum = pw3_bias[oc];
            for (int ic = 0; ic < 16; ic++)
                sum += pw3_weight[oc][ic][0] * c1[ic][pos];
            c2[oc][pos] = (sum > 0) ? sum : 0;
        }
    }

    // pool3 (k=5)
    for (int c = 0; c < 8; c++)
        for (int p = 0; p < CNN_WINDOW_SIZE / 4 / 5; p++) {
            float sum = 0;
            for (int k = 0; k < 5; k++)
                sum += c2[c][5 * p + k];
            d1[c][p] = sum / 5.0f;
        }

    // dw4 (groups=8, k=3, pad=1)
    for (int c = 0; c < 8; c++) {
        float tmp[CNN_WINDOW_SIZE / 4 / 5];
        for (int pos = 0; pos < CNN_WINDOW_SIZE / 4 / 5; pos++) {
            float sum = dw4_bias[c];
            for (int k = 0; k < 3; k++) {
                int ip = pos + k - 1;
                if (ip >= 0 && ip < CNN_WINDOW_SIZE / 4 / 5)
                    sum += dw4_weight[c][0][k] * d1[c][ip];
            }
            tmp[pos] = (sum > 0) ? sum : 0;
        }
        memcpy(d2[c], tmp, sizeof(tmp));
    }

    // pw4 (k=1, 8→4)
    for (int oc = 0; oc < 4; oc++) {
        for (int pos = 0; pos < CNN_WINDOW_SIZE / 4 / 5; pos++) {
            float sum = pw4_bias[oc];
            for (int ic = 0; ic < 8; ic++)
                sum += pw4_weight[oc][ic][0] * d2[ic][pos];
            e1[oc][pos] = (sum > 0) ? sum : 0;
        }
    }

    // flatten (4 × 5 = 20)
    float flat[20];
    for (int c = 0; c < 4; c++)
        for (int p = 0; p < 5; p++)
            flat[c * 5 + p] = e1[c][p];

    // fc1 (20→8) + ReLU
    float fc1_out[8];
    for (int oc = 0; oc < 8; oc++) {
        float sum = fc1_bias[oc];
        for (int ic = 0; ic < 20; ic++)
            sum += fc1_weight[oc][ic] * flat[ic];
        fc1_out[oc] = (sum > 0) ? sum : 0;
    }

    // fc2 (8→1)
    float logit = fc2_bias;
    for (int i = 0; i < 8; i++)
        logit += fc2_weight[i] * fc1_out[i];

    return 1.0f / (1.0f + expf(-logit));
}

void PPG_V3_Init(void)
{
    ir_emean = 0.0f;
    red_emean = 0.0f;
    memset(ir_hp_buf, 0, sizeof(ir_hp_buf));
    memset(red_hp_buf, 0, sizeof(red_hp_buf));
    buf_idx = 0;
    sample_count = 0;

    memset(int_buf, 0, sizeof(int_buf));
    int_idx = 0;
    int_sum = 0.0f;
    last_beat_sample = 0;
    hr_interval_sma = 0.0f;
    dc_ir_slow = 0.0f;
    proc_smooth = 0.0f;

    spo2_ac_ir_rms = 0.0f;
    spo2_ac_red_rms = 0.0f;
    spo2_dc_ir = 0.0f;
    spo2_dc_red = 0.0f;
    spo2_sample_count = 0;
    PPG_SetHR(0);
    PPG_SetSpO2(0);
}

void PPG_V3_Process(uint16_t ir_raw, uint16_t red_raw)
{
    float x = (float)ir_raw;

    // EWA HPF
    ir_emean = HP_ALPHA * x + (1.0f - HP_ALPHA) * ir_emean;
    float ir_hp = x - ir_emean;

    float r = (float)red_raw;
    red_emean = HP_ALPHA * r + (1.0f - HP_ALPHA) * red_emean;
    float red_hp = r - red_emean;

    // SpO2: track AC RMS and DC of both channels
    float abs_ir_hp = fabsf(ir_hp);
    float abs_red_hp = fabsf(red_hp);
    spo2_ac_ir_rms = 0.995f * spo2_ac_ir_rms + 0.005f * abs_ir_hp;
    spo2_ac_red_rms = 0.995f * spo2_ac_red_rms + 0.005f * abs_red_hp;
    spo2_dc_ir = 0.999f * spo2_dc_ir + 0.001f * x;
    spo2_dc_red = 0.999f * spo2_dc_red + 0.001f * r;
    spo2_sample_count++;

    // Store in circular buffer
    ir_hp_buf[buf_idx] = ir_hp;
    red_hp_buf[buf_idx] = red_hp;
    buf_idx = (buf_idx + 1) % CNN_WINDOW_SIZE;
    sample_count++;

    if (sample_count >= CNN_WINDOW_SIZE) {
        // Extract window in chronological order
        int start = buf_idx;
        for (int i = 0; i < CNN_WINDOW_SIZE; i++) {
            int p = (start + i) % CNN_WINDOW_SIZE;
            in[0][i] = ir_hp_buf[p];
            in[1][i] = red_hp_buf[p];
        }

        // Normalize: z-score over concat(IR, RED)
        float sum = 0, sum2 = 0;
        for (int i = 0; i < CNN_WINDOW_SIZE; i++) {
            float vi = in[0][i], vr = in[1][i];
            sum += vi;  sum2 += vi * vi;
            sum += vr;  sum2 += vr * vr;
        }
        float mean = sum / (2.0f * CNN_WINDOW_SIZE);
        float var = sum2 / (2.0f * CNN_WINDOW_SIZE) - mean * mean;
        float std = sqrtf(fmaxf(var, 0)) + 1e-6f;
        for (int i = 0; i < CNN_WINDOW_SIZE; i++) {
            in[0][i] = (in[0][i] - mean) / std;
            in[1][i] = (in[1][i] - mean) / std;
        }

        // CNN forward
        float prob_raw = cnn_forward();

        // Box filter integration (k=20)
        int_sum += prob_raw - int_buf[int_idx];
        int_buf[int_idx] = prob_raw;
        int_idx = (int_idx + 1) % INT_K;
        float p_smooth = int_sum / (float)INT_K;

        // Beat detection
        bool beat = false;
        if (p_smooth >= THRESHOLD && (sample_count - last_beat_sample) >= REFRACTORY) {
            beat = true;
            uint32_t interval = sample_count - last_beat_sample;
            if (last_beat_sample > 0) {
                if (hr_interval_sma == 0)
                    hr_interval_sma = (float)interval;
                else
                    hr_interval_sma = HR_SMA * interval + (1.0f - HR_SMA) * hr_interval_sma;
                float hr = 6000.0f / hr_interval_sma;
                hr = hr + 0.5f;
                if (hr < 30) hr = 30;
                if (hr > 240) hr = 240;
                PPG_SetHR((uint8_t)hr);
            }
            last_beat_sample = sample_count;
        }

        // SpO2 calculation (update every frame ≈ 1s at CNN_WINDOW_SIZE=200, 50Hz = 4s)
        if (spo2_sample_count > 0 && spo2_dc_ir > 100.0f && spo2_dc_red > 100.0f) {
            float ratio_ir = spo2_ac_ir_rms / spo2_dc_ir;
            float ratio_red = spo2_ac_red_rms / spo2_dc_red;
            if (ratio_ir > 0.0f) {
                float r = ratio_red / ratio_ir;
                float spo2 = 110.0f - 25.0f * r;
                if (spo2 > 100.0f) spo2 = 100.0f;
                if (spo2 < 70.0f) spo2 = 70.0f;
                PPG_SetSpO2((uint8_t)(spo2 + 0.5f));
            }
        }

        // Write to shared display buffers
        PPG_WriteProcessedSample(proc_smooth, beat ? 1 : 0);
    }

    dc_ir_slow = 0.999f * dc_ir_slow + 0.001f * x;
    proc_smooth = 0.8f * proc_smooth + 0.2f * ir_hp;

    char line[64];
    int len = snprintf(line, sizeof(line), "IR,RED,%u,%u\n", ir_raw, red_raw);
    uart_write_bytes(UART_NUM_1, line, len);
}

bool PPG_V3_HasContact(void)
{
    return dc_ir_slow > 2000.0f;
}
