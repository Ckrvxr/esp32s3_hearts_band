#include <math.h>
#include <string.h>
#include "ppg.h"
#include "ppg_v2.h"

#define FS              100.0f
#define HR_MIN          38
#define HR_MAX          250
#define VAR_SMALL_HW    8
#define THRESH_RATIO    0.35f
#define SPIKE_RATIO     2.5f
#define HR_EMA_ALPHA    0.3f
#define AC_BUF_SIZE     600
#define MAX_GOOD_INT    5
#define HR_CROSS_TOL    0.15f

static float dc_w_ir, dc_ir_slow, proc_smooth;
static float dc_w_red;

static float ac_buffer[AC_BUF_SIZE];
static uint16_t ac_buf_pos;
static uint16_t ac_buf_count;

static float amp_positive;
static float amp_negative;
static float pos_th;
static float neg_th;

typedef enum {
    TRACK_IDLE,
    TRACK_PEAK,
    TRACK_TROUGH,
} track_state_t;
static track_state_t track_state;
static float track_val;
static uint16_t track_pos;

static uint16_t last_peak_pos;
static uint16_t last_trough_pos;
static float peak_hr;
static float trough_hr;
static float hr_estimate;

static uint16_t good_intervals[MAX_GOOD_INT];
static uint8_t good_idx, good_count;
static uint16_t last_good_interval;

static uint32_t marker_counter;
static uint32_t marker_interval;
static bool motion_active;

static uint32_t sample_counter;
static uint32_t last_good_peak_time;

static float compute_variance(uint16_t center, uint16_t hw)
{
    float sum = 0.0f, sum2 = 0.0f;
    int count = 0;

    for (int d = -(int)hw; d <= (int)hw; d++) {
        int idx = (int)center + d;
        if (idx < 0) idx += AC_BUF_SIZE;
        if (idx >= AC_BUF_SIZE) idx -= AC_BUF_SIZE;
        float v = ac_buffer[(uint16_t)idx];
        sum += v;
        sum2 += v * v;
        count++;
    }

    if (count < 3) return 0.0f;
    float mean = sum / (float)count;
    float var = (sum2 / (float)count) - (mean * mean);
    return (var < 0.0f) ? 0.0f : var;
}

static uint16_t calc_interval(uint16_t current, uint16_t last)
{
    if (current >= last)
        return current - last;
    return current + AC_BUF_SIZE - last;
}

static void update_hr(float hr)
{
    hr_estimate = HR_EMA_ALPHA * hr + (1.0f - HR_EMA_ALPHA) * hr_estimate;
    if (hr_estimate < (float)HR_MIN) hr_estimate = (float)HR_MIN;
    if (hr_estimate > (float)HR_MAX) hr_estimate = (float)HR_MAX;
    ppg_hr = (uint8_t)(hr_estimate + 0.5f);
    marker_interval = (uint32_t)(6000.0f / hr_estimate);
    if (marker_interval < 1) marker_interval = 1;
}

static bool motion_check(uint16_t pos)
{
    float var_small = compute_variance(pos, VAR_SMALL_HW);
    uint16_t large_hw = (uint16_t)(1.5f * (float)last_good_interval);
    if (large_hw < VAR_SMALL_HW) large_hw = VAR_SMALL_HW;
    if (large_hw > AC_BUF_SIZE / 2) large_hw = AC_BUF_SIZE / 2;
    float var_large = compute_variance(pos, large_hw);

    if (var_large < 10.0f && var_small < 10.0f) return true;
    return (var_small > var_large * 3.0f);
}

static void store_good_interval(uint16_t interval)
{
    if (good_count < MAX_GOOD_INT) good_count++;
    good_intervals[good_idx] = interval;
    good_idx = (good_idx + 1) % MAX_GOOD_INT;
    last_good_interval = interval;
}

static uint16_t avg_good_interval(void)
{
    if (good_count == 0) return 0;
    uint32_t sum = 0;
    for (uint8_t i = 0; i < good_count; i++) sum += good_intervals[i];
    return (uint16_t)(sum / good_count);
}

static void process_peak(uint16_t pos, float val)
{
    (void)val;
    uint16_t interval = calc_interval(pos, last_peak_pos);
    last_peak_pos = pos;

    if (interval < 2) return;
    float hr = 6000.0f / (float)interval;
    if (hr < (float)HR_MIN || hr > (float)HR_MAX) return;

    bool cross_ok = true;
    if (trough_hr > 0.0f) {
        float diff = fabsf(hr - trough_hr) / fmaxf(hr, trough_hr);
        cross_ok = (diff < HR_CROSS_TOL);
    }

    if (!cross_ok) {
        motion_active = true;
        if (good_count > 0) {
            uint16_t avg_int = avg_good_interval();
            float hr_from_avg = 6000.0f / (float)avg_int;
            update_hr(hr_from_avg);
        }
        peak_hr = 0.0f;
        return;
    }

    bool motion = motion_check(pos);
    if (motion) {
        motion_active = true;
        if (good_count > 0) {
            uint16_t avg_int = avg_good_interval();
            float hr_from_avg = 6000.0f / (float)avg_int;
            update_hr(hr_from_avg);
        }
        return;
    }

    store_good_interval(interval);
    update_hr(hr);
    last_good_interval = interval;
    motion_active = false;
    last_good_peak_time = sample_counter;
    peak_hr = hr;
}

static void process_trough(uint16_t pos, float val)
{
    (void)val;
    uint16_t interval = calc_interval(pos, last_trough_pos);
    last_trough_pos = pos;

    if (interval < 2) return;
    float hr = 6000.0f / (float)interval;
    if (hr < (float)HR_MIN || hr > (float)HR_MAX) return;

    bool cross_ok = true;
    if (peak_hr > 0.0f) {
        float diff = fabsf(hr - peak_hr) / fmaxf(hr, peak_hr);
        cross_ok = (diff < HR_CROSS_TOL);
    }

    if (!cross_ok) {
        trough_hr = 0.0f;
        return;
    }

    bool motion = motion_check(pos);
    if (motion) return;

    trough_hr = hr;
}

void PPG_V2_Init(void)
{
    dc_w_ir = 0.0f;
    dc_w_red = 0.0f;
    dc_ir_slow = 0.0f;
    proc_smooth = 0.0f;

    memset(ac_buffer, 0, sizeof(ac_buffer));
    ac_buf_pos = 0;
    ac_buf_count = 0;

    amp_positive = 30.0f;
    amp_negative = 30.0f;
    pos_th = 15.0f;
    neg_th = 15.0f;
    track_state = TRACK_IDLE;
    track_val = 0.0f;
    track_pos = 0;

    last_peak_pos = 0;
    last_trough_pos = 0;
    peak_hr = 0.0f;
    trough_hr = 0.0f;
    hr_estimate = 72.0f;

    good_idx = 0;
    good_count = 0;
    last_good_interval = 42;

    marker_counter = 0;
    marker_interval = 0;

    sample_counter = 0;
    last_good_peak_time = 0;
    motion_active = false;
    ppg_hr = 0;
}

bool PPG_V2_HasContact(void)
{
    return dc_ir_slow > 2000.0f;
}

void PPG_V2_Process(uint16_t ir_raw, uint16_t red_raw)
{
    float x_ir = (float)ir_raw;
    float w_ir = x_ir + 0.98f * dc_w_ir;
    float ac_ir = w_ir - dc_w_ir;
    dc_w_ir = w_ir;

    float x_red = (float)red_raw;
    float w_red = x_red + 0.98f * dc_w_red;
    dc_w_red = w_red;

    dc_ir_slow = 0.999f * dc_ir_slow + 0.001f * x_ir;

    ac_buffer[ac_buf_pos] = ac_ir;
    uint16_t current_idx = ac_buf_pos;
    ac_buf_pos = (ac_buf_pos + 1) % AC_BUF_SIZE;
    if (ac_buf_count < AC_BUF_SIZE) ac_buf_count++;

    float abs_ac = fabsf(ac_ir);
    float running_max = fmaxf(amp_positive, amp_negative);

    if (abs_ac > running_max * SPIKE_RATIO) {
        amp_positive *= 0.9995f;
        amp_negative *= 0.9995f;
    } else {
        if (ac_ir > amp_positive)
            amp_positive = ac_ir;
        else
            amp_positive *= 0.9995f;

        if (-ac_ir > amp_negative)
            amp_negative = -ac_ir;
        else
            amp_negative *= 0.9995f;
    }

    if (amp_positive < 20.0f) amp_positive = 20.0f;
    if (amp_negative < 20.0f) amp_negative = 20.0f;
    pos_th = amp_positive * THRESH_RATIO;
    neg_th = amp_negative * THRESH_RATIO;

    switch (track_state) {
        case TRACK_IDLE:
            if (ac_ir > pos_th) {
                track_state = TRACK_PEAK;
                track_val = ac_ir;
                track_pos = current_idx;
            } else if (ac_ir < -neg_th) {
                track_state = TRACK_TROUGH;
                track_val = ac_ir;
                track_pos = current_idx;
            }
            break;

        case TRACK_PEAK:
            if (ac_ir > track_val) {
                track_val = ac_ir;
                track_pos = current_idx;
            }
            if (ac_ir < pos_th) {
                track_state = TRACK_IDLE;
                process_peak(track_pos, track_val);
            }
            break;

        case TRACK_TROUGH:
            if (ac_ir < track_val) {
                track_val = ac_ir;
                track_pos = current_idx;
            }
            if (ac_ir > -neg_th) {
                track_state = TRACK_IDLE;
                process_trough(track_pos, track_val);
            }
            break;
    }

    uint32_t expected = (last_good_interval > 0) ? (uint32_t)last_good_interval : 42;
    if (sample_counter - last_good_peak_time > 2 * expected) {
        amp_positive = 30.0f;
        amp_negative = 30.0f;
        pos_th = 15.0f;
        neg_th = 15.0f;
        motion_active = true;
        trough_hr = 0.0f;
    }
    sample_counter++;

    proc_smooth = 0.8f * proc_smooth + 0.2f * ac_ir;

    bool beat = false;
    uint8_t beat_type = 0;
    if (marker_interval > 0) {
        marker_counter++;
        if (marker_counter >= marker_interval) {
            marker_counter -= marker_interval;
            beat = true;
            beat_type = motion_active ? 2 : 1;
        }
    }

    uint16_t idx = (ppg_buf_head == 0) ? (PPG_SAMPLE_BUF - 1) : (ppg_buf_head - 1);
    xSemaphoreTake(ppg_mutex, portMAX_DELAY);
    ppg_proc_buf[idx] = proc_smooth;
    ppg_beat_buf[idx] = beat ? beat_type : 0;
    xSemaphoreGive(ppg_mutex);
}
