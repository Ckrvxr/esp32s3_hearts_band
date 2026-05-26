#include <math.h>
#include <string.h>
#include "ppg.h"
#include "ppg_v2.h"

#define FFT_N       512
#define FFT_LOG     9
#define FFT_ANALYZE_INTERVAL 200
#define FS          100.0f

#define HR_PEAK_LOW_BIN  3
#define HR_PEAK_HIGH_BIN 20

#define PSNR_THRESHOLD   6.0f

// ============================================================
// DCRemover
// ============================================================
static float dc_w_ir, dc_w_red;
static float dc_ir_slow, dc_red_slow;

// ============================================================
// AC ring buffer for FFT
// ============================================================
static float ir_ac_buf[FFT_N];
static float red_ac_buf[FFT_N];
static uint16_t ac_buf_pos;
static uint16_t ac_buf_count;

// ============================================================
// FFT work buffers (reused)
// ============================================================
static float fft_real[FFT_N];
static float fft_imag[FFT_N];
static float hanning[FFT_N];

// ============================================================
// FFT timing
// ============================================================
static uint32_t total_samples;
static uint32_t last_fft_sample;

// ============================================================
// Beat marker generation (synthetic, based on HR period)
// ============================================================
static uint32_t marker_counter;
static uint32_t marker_interval;
static float proc_smooth;

// ============================================================
// SpO₂ LUT
// ============================================================
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
// Bit-reversal permutation
// ============================================================
static void bit_reverse(float *real, float *imag, int n, int log_n)
{
    for (int i = 0; i < n; i++) {
        int j = 0;
        int x = i;
        for (int b = 0; b < log_n; b++) {
            j = (j << 1) | (x & 1);
            x >>= 1;
        }
        if (j > i) {
            float t = real[i]; real[i] = real[j]; real[j] = t;
            t = imag[i]; imag[i] = imag[j]; imag[j] = t;
        }
    }
}

// ============================================================
// Radix-2 DIT FFT (in-place)
// ============================================================
static void fft_calc(float *real, float *imag, int n, int log_n)
{
    bit_reverse(real, imag, n, log_n);

    for (int len = 2; len <= n; len <<= 1) {
        int m = len >> 1;
        float w_r = cosf((float)M_PI / (float)m);
        float w_i = -sinf((float)M_PI / (float)m);

        for (int i = 0; i < n; i += len) {
            float wr = 1.0f, wi = 0.0f;
            for (int j = 0; j < m; j++) {
                float tr = wr * real[i + j + m] - wi * imag[i + j + m];
                float ti = wr * imag[i + j + m] + wi * real[i + j + m];
                real[i + j + m] = real[i + j] - tr;
                imag[i + j + m] = imag[i + j] - ti;
                real[i + j] += tr;
                imag[i + j] += ti;

                float tmp = wr * w_r - wi * w_i;
                wi = wr * w_i + wi * w_r;
                wr = tmp;
            }
        }
    }
}

// ============================================================
// FFT analysis - compute HR and SpO₂
// ============================================================
static void fft_analyze(void)
{
    // ---- IR FFT ----
    memcpy(fft_real, ir_ac_buf, sizeof(float) * FFT_N);
    for (int i = 0; i < FFT_N; i++) fft_real[i] *= hanning[i];
    memset(fft_imag, 0, sizeof(float) * FFT_N);
    fft_calc(fft_real, fft_imag, FFT_N, FFT_LOG);

    // Power spectrum + find peak
    int k_peak = HR_PEAK_LOW_BIN;
    float p_peak = 0.0f;
    float p_sum = 0.0f;

    for (int k = HR_PEAK_LOW_BIN; k <= HR_PEAK_HIGH_BIN; k++) {
        float p = fft_real[k] * fft_real[k] + fft_imag[k] * fft_imag[k];
        p_sum += p;
        if (p > p_peak) {
            p_peak = p;
            k_peak = k;
        }
    }

    // Save IR FFT values at peak
    float ir_real = fft_real[k_peak];
    float ir_imag = fft_imag[k_peak];

    // ---- RED FFT ----
    memcpy(fft_real, red_ac_buf, sizeof(float) * FFT_N);
    for (int i = 0; i < FFT_N; i++) fft_real[i] *= hanning[i];
    memset(fft_imag, 0, sizeof(float) * FFT_N);
    fft_calc(fft_real, fft_imag, FFT_N, FFT_LOG);

    float red_real = fft_real[k_peak];
    float red_imag = fft_imag[k_peak];

    // ---- Parabolic interpolation ----
    float k_exact = (float)k_peak;
    if (k_peak > HR_PEAK_LOW_BIN && k_peak < HR_PEAK_HIGH_BIN) {
        float ym1 = fft_real[k_peak - 1] * fft_real[k_peak - 1] + fft_imag[k_peak - 1] * fft_imag[k_peak - 1];
        float y0  = p_peak;
        float y1  = fft_real[k_peak + 1] * fft_real[k_peak + 1] + fft_imag[k_peak + 1] * fft_imag[k_peak + 1];
        float denom = 2.0f * (ym1 - 2.0f * y0 + y1);
        if (fabsf(denom) > 1e-10f) {
            float delta = (ym1 - y1) / denom;
            if (delta > 0.5f) delta = 0.5f;
            if (delta < -0.5f) delta = -0.5f;
            k_exact = (float)k_peak + delta;
        }
    }

    float f_hr = k_exact * FS / (float)FFT_N;
    float hr = f_hr * 60.0f;
    if (hr < 30.0f) hr = 30.0f;
    if (hr > 240.0f) hr = 240.0f;

    // ---- Signal quality (PSNR) ----
    int k_count = HR_PEAK_HIGH_BIN - HR_PEAK_LOW_BIN + 1;
    float p_mean = p_sum / (float)k_count;
    float psnr = 10.0f * log10f(p_peak / (p_mean + 1e-10f));

    if (psnr > PSNR_THRESHOLD) {
        ppg_hr = (uint8_t)(hr + 0.5f);
        marker_interval = (uint32_t)(6000.0f / (float)ppg_hr);
        if (marker_interval < 1) marker_interval = 1;

        // ---- SpO₂ ----
        float mag_ir = sqrtf(ir_real * ir_real + ir_imag * ir_imag);
        float mag_red = sqrtf(red_real * red_real + red_imag * red_imag);

        if (mag_ir > 1.0f && mag_red > 1.0f && dc_ir_slow > 100.0f && dc_red_slow > 100.0f) {
            float R_log = 200.0f * logf(mag_red) / logf(mag_ir);
            ppg_spo2 = spo2_lookup(R_log);
        }
    }
}

// ============================================================
// Public API
// ============================================================
void PPG_V2_Init(void)
{
    for (int i = 0; i < FFT_N; i++) {
        hanning[i] = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * (float)i / (float)(FFT_N - 1));
    }

    dc_w_ir = 0.0f;
    dc_w_red = 0.0f;
    dc_ir_slow = 0.0f;
    dc_red_slow = 0.0f;

    memset(ir_ac_buf, 0, sizeof(ir_ac_buf));
    memset(red_ac_buf, 0, sizeof(red_ac_buf));
    ac_buf_pos = 0;
    ac_buf_count = 0;

    total_samples = 0;
    last_fft_sample = 0;

    marker_counter = 0;
    marker_interval = 0;
    proc_smooth = 0.0f;

    ppg_hr = 0;
    ppg_spo2 = 99;
}

void PPG_V2_Process(uint16_t ir_raw, uint16_t red_raw)
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
    // 2. Store AC in ring buffer
    // --------------------------------------------------------
    ir_ac_buf[ac_buf_pos] = ac_ir;
    red_ac_buf[ac_buf_pos] = ac_red;
    ac_buf_pos = (ac_buf_pos + 1) % FFT_N;
    if (ac_buf_count < FFT_N) ac_buf_count++;

    // --------------------------------------------------------
    // 3. Smooth display waveform
    // --------------------------------------------------------
    proc_smooth = 0.8f * proc_smooth + 0.2f * ac_ir;

    // --------------------------------------------------------
    // 4. Beat marker (synthetic)
    // --------------------------------------------------------
    bool beat = false;
    if (marker_interval > 0) {
        marker_counter++;
        if (marker_counter >= marker_interval) {
            marker_counter -= marker_interval;
            beat = true;
        }
    }

    // --------------------------------------------------------
    // 5. Store to shared buffers
    // --------------------------------------------------------
    uint16_t idx = (ppg_buf_head == 0) ? (PPG_SAMPLE_BUF - 1) : (ppg_buf_head - 1);

    xSemaphoreTake(ppg_mutex, portMAX_DELAY);
    ppg_proc_buf[idx] = proc_smooth;
    if (beat) {
        ppg_beat_buf[idx] = 1;
    }
    xSemaphoreGive(ppg_mutex);

    // --------------------------------------------------------
    // 6. Periodic FFT analysis
    // --------------------------------------------------------
    total_samples++;
    if (ac_buf_count >= FFT_N && total_samples - last_fft_sample >= FFT_ANALYZE_INTERVAL) {
        fft_analyze();
        last_fft_sample = total_samples;
    }
}
