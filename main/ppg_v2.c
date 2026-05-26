#include <math.h>
#include <string.h>
#include "ppg.h"
#include "ppg_v2.h"

#define FS              100.0f
#define FFT_N           512
#define FFT_HALF        256
#define FFT_STEP        256
#define HR_MIN          30
#define HR_MAX          240
#define HR_EMA_ALPHA    0.4f
#define CONF_THRESH     30.0f
#define TIMEOUT_BLANK   2

static float dc_block_y, x_prev;
static float fft_buf[FFT_N];
static float hanning[FFT_N];
static float fft_work[FFT_N * 2];
static uint16_t fft_buf_idx;
static uint32_t fft_sample_count;
static float hr_ema;
static uint8_t no_signal_count;
static uint32_t marker_counter;
static uint32_t marker_interval;
static bool fft_valid;
static float dc_ir_slow;
static float proc_smooth;

static void fft_radix2(float *data, int n)
{
    int j = 0;
    for (int i = 0; i < n; i++) {
        if (i < j) {
            float tr = data[j*2], ti = data[j*2+1];
            data[j*2] = data[i*2]; data[j*2+1] = data[i*2+1];
            data[i*2] = tr; data[i*2+1] = ti;
        }
        int m = n / 2;
        while (m >= 1 && j >= m) { j -= m; m /= 2; }
        j += m;
    }

    for (int len = 2; len <= n; len <<= 1) {
        float w_angle = -2.0f * (float)M_PI / len;
        float wr = cosf(w_angle), wi = sinf(w_angle);
        for (int i = 0; i < n; i += len) {
            float twr = 1.0f, twi = 0.0f;
            for (int k = 0; k < len / 2; k++) {
                int i1 = i + k, i2 = i + k + len / 2;
                float t_r = twr * data[i2*2] - twi * data[i2*2+1];
                float t_i = twr * data[i2*2+1] + twi * data[i2*2];
                data[i2*2] = data[i1*2] - t_r;
                data[i2*2+1] = data[i1*2+1] - t_i;
                data[i1*2] += t_r;
                data[i1*2+1] += t_i;
                float nwr = twr * wr - twi * wi;
                twi = twr * wi + twi * wr;
                twr = nwr;
            }
        }
    }
}

void PPG_V2_Init(void)
{
    dc_block_y = 0.0f;
    x_prev = 0.0f;
    memset(fft_buf, 0, sizeof(fft_buf));
    for (int i = 0; i < FFT_N; i++)
        hanning[i] = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * i / (FFT_N - 1));
    fft_buf_idx = 0;
    fft_sample_count = 0;
    hr_ema = 72.0f;
    no_signal_count = TIMEOUT_BLANK;
    marker_counter = 0;
    marker_interval = 0;
    fft_valid = false;
    dc_ir_slow = 0.0f;
    proc_smooth = 0.0f;
    ppg_hr = 0;
}

bool PPG_V2_HasContact(void)
{
    return dc_ir_slow > 2000.0f;
}

void PPG_V2_Process(uint16_t ir_raw, uint16_t red_raw)
{
    (void)red_raw;
    float x = (float)ir_raw;

    float y_dc = x - x_prev + 0.97f * dc_block_y;
    dc_block_y = y_dc;
    x_prev = x;

    fft_buf[fft_buf_idx] = y_dc;
    fft_buf_idx = (fft_buf_idx + 1) % FFT_N;
    fft_sample_count++;

    if (fft_sample_count >= FFT_N && (fft_sample_count % FFT_STEP == 0)) {
        int pos = (fft_buf_idx + FFT_N - FFT_STEP) % FFT_N;
        for (int i = 0; i < FFT_N; i++) {
            int idx = (pos + i) % FFT_N;
            fft_work[i * 2] = fft_buf[idx] * hanning[i];
            fft_work[i * 2 + 1] = 0.0f;
        }

        fft_radix2(fft_work, FFT_N);

        int k_peak = 3;
        float mag_max = 0.0f;
        for (int k = 3; k <= 20; k++) {
            float re = fft_work[k * 2], im = fft_work[k * 2 + 1];
            float mag = sqrtf(re * re + im * im);
            if (mag > mag_max) { mag_max = mag; k_peak = k; }
        }

        float k_exact = (float)k_peak;
        if (mag_max > CONF_THRESH && k_peak > 0 && k_peak < FFT_HALF) {
            float re_m = fft_work[(k_peak - 1) * 2], im_m = fft_work[(k_peak - 1) * 2 + 1];
            float re_p = fft_work[(k_peak + 1) * 2], im_p = fft_work[(k_peak + 1) * 2 + 1];
            float m1 = sqrtf(re_m * re_m + im_m * im_m);
            float m3 = sqrtf(re_p * re_p + im_p * im_p);
            float denom = 2.0f * mag_max - m1 - m3;
            if (fabsf(denom) > 1e-10f) {
                float d = 0.5f * (m3 - m1) / denom;
                if (d > -0.5f && d < 0.5f) k_exact += d;
            }
        }

        float hr = k_exact * FS / FFT_N * 60.0f;

        if (hr >= (float)HR_MIN && hr <= (float)HR_MAX && mag_max > CONF_THRESH) {
            hr_ema = HR_EMA_ALPHA * hr + (1.0f - HR_EMA_ALPHA) * hr_ema;
            ppg_hr = (uint8_t)(hr_ema + 0.5f);
            if (ppg_hr < 1) ppg_hr = 1;
            marker_interval = (uint32_t)(6000.0f / hr_ema);
            if (marker_interval < 1) marker_interval = 1;
            marker_counter = 0;
            no_signal_count = 0;
            fft_valid = true;
        } else {
            no_signal_count++;
            fft_valid = false;
            if (no_signal_count >= TIMEOUT_BLANK) ppg_hr = 0;
        }
    }

    dc_ir_slow = 0.999f * dc_ir_slow + 0.001f * x;

    proc_smooth = 0.8f * proc_smooth + 0.2f * y_dc;

    bool beat = false;
    uint8_t beat_type = 0;
    if (marker_interval > 0 && ppg_hr > 0) {
        marker_counter++;
        if (marker_counter >= marker_interval) {
            marker_counter = 0;
            beat = true;
            beat_type = fft_valid ? 1 : 2;
        }
    }

    uint16_t idx = (ppg_buf_head == 0) ? (PPG_SAMPLE_BUF - 1) : (ppg_buf_head - 1);
    xSemaphoreTake(ppg_mutex, portMAX_DELAY);
    ppg_proc_buf[idx] = proc_smooth;
    ppg_beat_buf[idx] = beat ? beat_type : 0;
    xSemaphoreGive(ppg_mutex);
}
