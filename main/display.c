#include <string.h>
#include <math.h>

#include "esp_log.h"
#include "driver/i2c_master.h"
#include "rom/ets_sys.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "u8g2.h"

#include "display.h"
#include "ble.h"
#include "ppg.h"
#include "ppg_v3.h"

// ------------------------------------------------------ Driver -------------------------------------------------------
#define I2C_MASTER_SCL      4
#define I2C_MASTER_SDA      5
#define I2C_MASTER_FREQ_HZ  400000
#define I2C_ADDR_7BIT       0x3C
#define I2C_MASTER_TIMEOUT_MS 100

static const char *TAG = "DISPLAY";

volatile DisplayState_t currentState = STATE_MAIN_SCREEN;

static u8g2_t u8g2;
static uint32_t frame_count = 0;
static i2c_master_bus_handle_t bus_handle;
static i2c_master_dev_handle_t dev_handle;

#define HR_HIST_SIZE 120
static uint8_t hr_history[HR_HIST_SIZE];
static uint16_t hr_history_head;
static uint16_t hr_history_count;
static uint8_t hr_last_stored;
static float hr_smoothed;
static uint8_t i2c_tx_buf[1025];
static uint16_t i2c_buf_len = 0;

static uint8_t u8x8_byte_esp32_hw_i2c(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    switch (msg) {
        case U8X8_MSG_BYTE_INIT:
        case U8X8_MSG_BYTE_SET_DC:
            break;
        case U8X8_MSG_BYTE_START_TRANSFER:
            i2c_buf_len = 0;
            break;
        case U8X8_MSG_BYTE_SEND:
            if (i2c_buf_len + arg_int > sizeof(i2c_tx_buf)) {
                ESP_LOGE(TAG, "I2C buffer overflow");
                return 0;
            }
            memcpy(i2c_tx_buf + i2c_buf_len, arg_ptr, arg_int);
            i2c_buf_len += arg_int;
            break;
        case U8X8_MSG_BYTE_END_TRANSFER:
            if (i2c_buf_len > 0) {
                esp_err_t ret = i2c_master_transmit(dev_handle, i2c_tx_buf, i2c_buf_len, I2C_MASTER_TIMEOUT_MS);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "I2C transmit failed: %s", esp_err_to_name(ret));
                    return 0;
                }
            }
            break;
        default:
            return 0;
    }
    return 1;
}

static uint8_t u8x8_gpio_and_delay_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    switch (msg) {
        case U8X8_MSG_GPIO_AND_DELAY_INIT:
            break;
        case U8X8_MSG_DELAY_MILLI:
            vTaskDelay(pdMS_TO_TICKS(arg_int));
            break;
        case U8X8_MSG_DELAY_10MICRO:
            ets_delay_us(arg_int * 10);
            break;
        case U8X8_MSG_DELAY_100NANO:
            ets_delay_us(arg_int / 10);
            break;
        case U8X8_MSG_DELAY_NANO:
        case U8X8_MSG_DELAY_I2C:
            break;
        case U8X8_MSG_GPIO_RESET:
            break;
        default:
            return 0;
    }
    return 1;
}

void Display_Init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_MASTER_SDA,
        .scl_io_num = I2C_MASTER_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus_handle));

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = I2C_ADDR_7BIT,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle));

    u8g2_Setup_ssd1315_i2c_128x64_noname_f(&u8g2, U8G2_R0, u8x8_byte_esp32_hw_i2c, u8x8_gpio_and_delay_cb);
    u8g2_InitDisplay(&u8g2);
    u8g2_SetPowerSave(&u8g2, 0);
    vTaskDelay(pdMS_TO_TICKS(100));

    hr_history_head = 0;
    hr_history_count = 0;
    hr_last_stored = 0;
    hr_smoothed = 72.0f;
    ESP_LOGI(TAG, "Display initialized");
}

static void Display_Draw_MainScreen(void)
{
    u8g2_SetFont(&u8g2, u8g2_font_ncenB08_tr);
    u8g2_DrawStr(&u8g2, 8, 10, "Hearts Band");

    u8g2_DrawHLine(&u8g2, 0, 14, 128);

    u8g2_DrawStr(&u8g2, 8, 32, "Hello World !");
}

static void Display_Draw_PpgRaw6sAvg(void)
{
    u8g2_SetFont(&u8g2, u8g2_font_ncenB08_tr);
    u8g2_DrawStr(&u8g2, 8, 10, "PPG RAW (6s, Avg)");
    u8g2_DrawHLine(&u8g2, 0, 14, 128);

#define PPG_PLOT_X     4
#define PPG_PLOT_Y     18
#define PPG_PLOT_W     120
#define PPG_PLOT_H     44

    u8g2_DrawFrame(&u8g2, PPG_PLOT_X - 1, PPG_PLOT_Y - 1, PPG_PLOT_W + 2, PPG_PLOT_H + 2);

    uint16_t count, head;
    uint16_t ir_min = 0xFFFF, ir_max = 0;
    uint16_t red_min = 0xFFFF, red_max = 0;
    uint16_t last_ir = 0, last_red = 0;
    {
        xSemaphoreTake(ppg_mutex, portMAX_DELAY);
        count = ppg_buf_count;
        head = ppg_buf_head;
        for (uint16_t i = 0; i < count; i++) {
            uint16_t idx = (head + PPG_SAMPLE_BUF - count + i) % PPG_SAMPLE_BUF;
            uint16_t v = ppg_ir_buf[idx];
            if (v < ir_min) ir_min = v;
            if (v > ir_max) ir_max = v;
            v = ppg_red_buf[idx];
            if (v < red_min) red_min = v;
            if (v > red_max) red_max = v;
            last_ir = ppg_ir_buf[idx];
            last_red = ppg_red_buf[idx];
        }
        xSemaphoreGive(ppg_mutex);
    }

    if (count < 2) return;
    if (ir_max == ir_min) ir_max = ir_min + 1;
    if (red_max == red_min) red_max = red_min + 1;

    uint16_t start = (count <= PPG_PLOT_W) ? 0 : (head + PPG_SAMPLE_BUF - count) % PPG_SAMPLE_BUF;
    uint16_t plot_n = count;
    uint16_t decimation = (plot_n + PPG_PLOT_W - 1) / PPG_PLOT_W;

    int prev_ir_y = -1;
    int prev_red_y = -1;
    int prev_x = -1;

    for (int col = 0; col < PPG_PLOT_W; col++) {
        uint16_t lo = col * decimation;
        uint16_t hi = lo + decimation;
        if (hi > plot_n) hi = plot_n;
        if (lo >= plot_n) break;

        int x = PPG_PLOT_X + col;
        uint32_t ir_sum = 0;
        uint32_t re_sum = 0;
        uint16_t n_win = 0;
        uint8_t beat = 0;

        xSemaphoreTake(ppg_mutex, portMAX_DELAY);
        for (uint16_t j = lo; j < hi; j++) {
            uint16_t idx = (start + j) % PPG_SAMPLE_BUF;
            ir_sum += ppg_ir_buf[idx];
            re_sum += ppg_red_buf[idx];
            n_win++;
            uint8_t b = ppg_beat_buf[idx];
            if (b) beat = b;
        }
        xSemaphoreGive(ppg_mutex);

        if (n_win == 0) continue;

        uint16_t ir_mean = ir_sum / n_win;
        uint16_t re_mean = re_sum / n_win;

        int y_ir = PPG_PLOT_Y + PPG_PLOT_H - 1 -
                   (uint32_t)(ir_mean - ir_min) * (PPG_PLOT_H - 1) / (ir_max - ir_min);
        int y_re = PPG_PLOT_Y + PPG_PLOT_H - 1 -
                   (uint32_t)(re_mean - red_min) * (PPG_PLOT_H - 1) / (red_max - red_min);

        if (prev_ir_y >= 0 && prev_x >= 0) {
            u8g2_DrawLine(&u8g2, prev_x, prev_ir_y, x, y_ir);
        } else {
            u8g2_DrawPixel(&u8g2, x, y_ir);
        }
        prev_ir_y = y_ir;

        if (col % 2 == 0) {
            u8g2_DrawPixel(&u8g2, x, y_re);
        }
        if (col % 2 == 0 && prev_red_y >= 0 && prev_x >= 0) {
            u8g2_DrawLine(&u8g2, prev_x, prev_red_y, x, y_re);
        }
        prev_red_y = y_re;
        prev_x = x;

        if (beat == 1) {
            u8g2_DrawVLine(&u8g2, x, PPG_PLOT_Y, PPG_PLOT_H - 1);
        } else if (beat == 2) {
            int seg = (PPG_PLOT_H - 1) / 3;
            u8g2_DrawVLine(&u8g2, x, PPG_PLOT_Y, seg);
            u8g2_DrawVLine(&u8g2, x, PPG_PLOT_Y + PPG_PLOT_H - 1 - seg, seg);
        }
    }

    char buf[32];
    snprintf(buf, sizeof(buf), "IR:%5u  RED:%5u", last_ir, last_red);
    u8g2_DrawStr(&u8g2, 8, 61, buf);
}

static void Display_Draw_PpgRaw1s(void)
{
    u8g2_SetFont(&u8g2, u8g2_font_ncenB08_tr);
    u8g2_DrawStr(&u8g2, 8, 10, "PPG RAW (1s)");
    u8g2_DrawHLine(&u8g2, 0, 14, 128);

#define R1S_PLOT_X     4
#define R1S_PLOT_Y     18
#define R1S_PLOT_W     120
#define R1S_PLOT_H     44
#define R1S_WINDOW     100

    u8g2_DrawFrame(&u8g2, R1S_PLOT_X - 1, R1S_PLOT_Y - 1, R1S_PLOT_W + 2, R1S_PLOT_H + 2);

    uint16_t count, head;
    uint16_t ir_min = 0xFFFF, ir_max = 0;
    uint16_t red_min = 0xFFFF, red_max = 0;
    uint16_t last_ir = 0, last_red = 0;
    {
        xSemaphoreTake(ppg_mutex, portMAX_DELAY);
        count = ppg_buf_count;
        head = ppg_buf_head;
        uint16_t n_scan = (count < R1S_WINDOW) ? count : R1S_WINDOW;
        for (uint16_t i = 0; i < n_scan; i++) {
            uint16_t idx = (head + PPG_SAMPLE_BUF - n_scan + i) % PPG_SAMPLE_BUF;
            uint16_t v = ppg_ir_buf[idx];
            if (v < ir_min) ir_min = v;
            if (v > ir_max) ir_max = v;
            v = ppg_red_buf[idx];
            if (v < red_min) red_min = v;
            if (v > red_max) red_max = v;
            last_ir = ppg_ir_buf[idx];
            last_red = ppg_red_buf[idx];
        }
        xSemaphoreGive(ppg_mutex);
    }

    if (count < 2) return;
    if (ir_max == ir_min) ir_max = ir_min + 1;
    if (red_max == red_min) red_max = red_min + 1;

    uint16_t plot_n = (count < R1S_WINDOW) ? count : R1S_WINDOW;
    uint16_t start = (head + PPG_SAMPLE_BUF - plot_n) % PPG_SAMPLE_BUF;
    int x_off = (R1S_PLOT_W - plot_n) / 2;

    int prev_ir_y = -1;
    int prev_red_y = -1;
    int prev_x = -1;

    for (int col = 0; col < plot_n; col++) {
        int x = R1S_PLOT_X + x_off + col;
        uint16_t idx = (start + col) % PPG_SAMPLE_BUF;
        uint8_t beat = 0;

        xSemaphoreTake(ppg_mutex, portMAX_DELAY);
        uint16_t ir_v = ppg_ir_buf[idx];
        uint16_t re_v = ppg_red_buf[idx];
        beat = ppg_beat_buf[idx];
        xSemaphoreGive(ppg_mutex);

        int y_ir = R1S_PLOT_Y + R1S_PLOT_H - 1 -
                   (uint32_t)(ir_v - ir_min) * (R1S_PLOT_H - 1) / (ir_max - ir_min);
        int y_re = R1S_PLOT_Y + R1S_PLOT_H - 1 -
                   (uint32_t)(re_v - red_min) * (R1S_PLOT_H - 1) / (red_max - red_min);

        if (prev_ir_y >= 0 && prev_x >= 0) {
            u8g2_DrawLine(&u8g2, prev_x, prev_ir_y, x, y_ir);
        } else {
            u8g2_DrawPixel(&u8g2, x, y_ir);
        }
        prev_ir_y = y_ir;

        if (col % 2 == 0) {
            u8g2_DrawPixel(&u8g2, x, y_re);
        }
        if (col % 2 == 0 && prev_red_y >= 0 && prev_x >= 0) {
            u8g2_DrawLine(&u8g2, prev_x, prev_red_y, x, y_re);
        }
        prev_red_y = y_re;
        prev_x = x;

        if (beat == 1) {
            u8g2_DrawVLine(&u8g2, x, R1S_PLOT_Y, R1S_PLOT_H - 1);
        } else if (beat == 2) {
            int seg = (R1S_PLOT_H - 1) / 3;
            u8g2_DrawVLine(&u8g2, x, R1S_PLOT_Y, seg);
            u8g2_DrawVLine(&u8g2, x, R1S_PLOT_Y + R1S_PLOT_H - 1 - seg, seg);
        }
    }

    char buf[32];
    snprintf(buf, sizeof(buf), "IR:%5u  RED:%5u", last_ir, last_red);
    u8g2_DrawStr(&u8g2, 8, 61, buf);
}

static void Display_Draw_Processed(void)
{
    u8g2_SetFont(&u8g2, u8g2_font_ncenB08_tr);
    u8g2_DrawStr(&u8g2, 8, 10, "PROCESSED");
    u8g2_DrawHLine(&u8g2, 0, 14, 128);

#define PDC_PLOT_X     4
#define PDC_PLOT_Y     18
#define PDC_PLOT_W     120
#define PDC_PLOT_H     44

    u8g2_DrawFrame(&u8g2, PDC_PLOT_X - 1, PDC_PLOT_Y - 1, PDC_PLOT_W + 2, PDC_PLOT_H + 2);
    uint16_t pcount;
    uint16_t phead;
    {
        xSemaphoreTake(ppg_mutex, portMAX_DELAY);
        pcount = ppg_buf_count;
        phead = ppg_buf_head;
        xSemaphoreGive(ppg_mutex);
    }

    if (pcount < 2) {
        char buf[32];
        snprintf(buf, sizeof(buf), "HR:%3u", ppg_hr);
        u8g2_DrawStr(&u8g2, 8, 62, buf);
        return;
    }

    float p_min = 1e10f, p_max = -1e10f;
    xSemaphoreTake(ppg_mutex, portMAX_DELAY);
    for (uint16_t i = 0; i < pcount; i++) {
        float v = ppg_proc_buf[(phead + PPG_SAMPLE_BUF - pcount + i) % PPG_SAMPLE_BUF];
        if (v < p_min) p_min = v;
        if (v > p_max) p_max = v;
    }
    xSemaphoreGive(ppg_mutex);

    float p_abs = fmaxf(fabsf(p_min), fabsf(p_max));
    if (p_abs < 1.0f) p_abs = 1.0f;
    float scale = (PDC_PLOT_H / 2.0f - 1.0f) / p_abs;
    int y_center = PDC_PLOT_Y + PDC_PLOT_H / 2;

    uint16_t pstart = (pcount <= PDC_PLOT_W) ? 0 : (phead + PPG_SAMPLE_BUF - pcount) % PPG_SAMPLE_BUF;
    uint16_t pplot_n = pcount;
    uint16_t pdec = (pplot_n + PDC_PLOT_W - 1) / PDC_PLOT_W;

    for (int col = 0; col < PDC_PLOT_W; col++) {
        uint16_t lo = col * pdec;
        uint16_t hi = lo + pdec;
        if (hi > pplot_n) hi = pplot_n;
        if (lo >= pplot_n) break;
        int x = PDC_PLOT_X + col;

        float f_lo = 1e10f, f_hi = -1e10f;
        xSemaphoreTake(ppg_mutex, portMAX_DELAY);
        for (uint16_t j = lo; j < hi; j++) {
            float v = ppg_proc_buf[(pstart + j) % PPG_SAMPLE_BUF];
            if (v < f_lo) f_lo = v;
            if (v > f_hi) f_hi = v;
        }
        xSemaphoreGive(ppg_mutex);

        int y_lo = y_center - (int)(f_hi * scale);
        int y_hi = y_center - (int)(f_lo * scale);
        if (y_lo < PDC_PLOT_Y) y_lo = PDC_PLOT_Y;
        if (y_hi >= PDC_PLOT_Y + PDC_PLOT_H) y_hi = PDC_PLOT_Y + PDC_PLOT_H - 1;
        if (y_lo != y_hi) {
            u8g2_DrawVLine(&u8g2, x, y_lo, y_hi - y_lo + 1);
        } else {
            u8g2_DrawPixel(&u8g2, x, y_lo);
        }
    }

    // Beat markers
    for (int col = 0; col < PDC_PLOT_W; col++) {
        uint16_t lo = col * pdec;
        uint16_t hi = lo + pdec;
        if (hi > pplot_n) hi = pplot_n;
        if (lo >= pplot_n) break;
        uint8_t beat = 0;
        xSemaphoreTake(ppg_mutex, portMAX_DELAY);
        for (uint16_t j = lo; j < hi && !beat; j++) {
            beat = ppg_beat_buf[(pstart + j) % PPG_SAMPLE_BUF];
        }
        xSemaphoreGive(ppg_mutex);
        if (beat == 1) {
            u8g2_DrawVLine(&u8g2, PDC_PLOT_X + col, PDC_PLOT_Y, PDC_PLOT_H - 1);
        } else if (beat == 2) {
            int seg = (PDC_PLOT_H - 1) / 3;
            u8g2_DrawVLine(&u8g2, PDC_PLOT_X + col, PDC_PLOT_Y, seg);
            u8g2_DrawVLine(&u8g2, PDC_PLOT_X + col, PDC_PLOT_Y + PDC_PLOT_H - 1 - seg, seg);
        }
    }

    char buf[32];
    snprintf(buf, sizeof(buf), "HR:%3u", ppg_hr);
    u8g2_DrawStr(&u8g2, 8, 62, buf);
}

// ============================================================
// ── 心率大字视图 ──
static void Display_Draw_HR(void)
{
    u8g2_SetFont(&u8g2, u8g2_font_ncenB08_tr);
    u8g2_DrawStr(&u8g2, 8, 10, "HEART RATE");
    u8g2_DrawHLine(&u8g2, 0, 14, 128);

#define BPM_PLOT_X     4
#define BPM_PLOT_Y     18
#define BPM_PLOT_W     120
#define BPM_PLOT_H     36

    if (hr_history_count > 0 && ppg_hr > 0) {
        u8g2_DrawFrame(&u8g2, BPM_PLOT_X - 1, BPM_PLOT_Y - 1, BPM_PLOT_W + 2, BPM_PLOT_H + 2);

        uint8_t bpm = ppg_hr;
        int half = (240 - 40) / 6;
        int win_lo = (int)bpm - half;
        int win_hi = (int)bpm + half;
        if (win_lo < 40) { win_hi += 40 - win_lo; win_lo = 40; }
        if (win_hi > 240) { win_lo -= win_hi - 240; win_hi = 240; }
        if (win_lo < 40) win_lo = 40;
        uint8_t y_min = (uint8_t)win_lo;
        uint8_t y_max = (uint8_t)win_hi;
        uint8_t y_span = y_max - y_min;
        if (y_span < 10) y_span = 10;

        uint16_t plot_n = (hr_history_count < BPM_PLOT_W) ? hr_history_count : BPM_PLOT_W;
        uint16_t start = (hr_history_count <= BPM_PLOT_W) ? 0
            : (hr_history_head + HR_HIST_SIZE - plot_n) % HR_HIST_SIZE;

        int prev_x = -1, prev_y = -1;
        int x_off = BPM_PLOT_X + (BPM_PLOT_W - plot_n);

        for (int col = 0; col < plot_n; col++) {
            uint16_t idx = (start + col) % HR_HIST_SIZE;
            uint8_t hr_val = hr_history[idx];

            int y = BPM_PLOT_Y + BPM_PLOT_H - 1
                  - (uint32_t)(hr_val - y_min) * (BPM_PLOT_H - 1) / y_span;
            int x = x_off + col;

            if (prev_x >= 0)
                u8g2_DrawLine(&u8g2, prev_x, prev_y, x, y);
            else
                u8g2_DrawPixel(&u8g2, x, y);

            prev_x = x;
            prev_y = y;
        }
    }

    char buf[16];
    if (ppg_hr > 0) {
        u8g2_SetFont(&u8g2, u8g2_font_ncenB10_tr);
        snprintf(buf, sizeof(buf), "%u", ppg_hr);
        u8g2_DrawStr(&u8g2, 88, 62, buf);
        u8g2_SetFont(&u8g2, u8g2_font_ncenB08_tr);
        u8g2_DrawStr(&u8g2, 110, 62, "BPM");
    } else {
        u8g2_SetFont(&u8g2, u8g2_font_ncenB10_tr);
        int x = (128 - u8g2_GetStrWidth(&u8g2, "--")) / 2;
        u8g2_DrawStr(&u8g2, x, 42, "--");
        u8g2_SetFont(&u8g2, u8g2_font_ncenB08_tr);
        x = (128 - u8g2_GetStrWidth(&u8g2, "NO SIGNAL")) / 2;
        u8g2_DrawStr(&u8g2, x, 58, "NO SIGNAL");
    }
}

// --------------------------------------------------- Timer Pages -----------------------------------------------------
static void Display_Draw_TimerSet(void)
{
    u8g2_SetFont(&u8g2, u8g2_font_ncenB08_tr);
    u8g2_DrawStr(&u8g2, 8, 10, "Timer Set");
    u8g2_DrawHLine(&u8g2, 0, 14, 128);

    u8g2_SetFont(&u8g2, u8g2_font_ncenB18_tr);
    u8g2_DrawStr(&u8g2, 16, 36, "OK!");

    u8g2_SetFont(&u8g2, u8g2_font_ncenB08_tr);
    u8g2_DrawStr(&u8g2, 28, 52, "Timer: 30min");
}

static void Display_Draw_TimerRunning(void)
{
    u8g2_SetFont(&u8g2, u8g2_font_ncenB08_tr);
    u8g2_DrawStr(&u8g2, 8, 10, "Countdown");
    u8g2_DrawHLine(&u8g2, 0, 14, 128);

    u8g2_SetFont(&u8g2, u8g2_font_ncenB18_tr);
    u8g2_DrawStr(&u8g2, 10, 42, "00:00");

    u8g2_DrawFrame(&u8g2, 4, 48, 120, 8);
    u8g2_DrawBox(&u8g2, 5, 49, 118, 6);
}

static void Display_Draw_TimerDone(void)
{
    u8g2_SetFont(&u8g2, u8g2_font_ncenB08_tr);

    if ((frame_count / 8) % 2) {
        u8g2_DrawStr(&u8g2, 32, 10, "TIME'S UP");
        u8g2_DrawHLine(&u8g2, 0, 14, 128);
    }

    u8g2_SetFont(&u8g2, u8g2_font_ncenB24_tr);
    u8g2_DrawStr(&u8g2, 28, 46, "!!!");

    u8g2_SetFont(&u8g2, u8g2_font_ncenB08_tr);
    u8g2_DrawStr(&u8g2, 36, 60, "Time Over");
}

// ── BLE Config Page ─────────────────────────────────────────────────
static void Display_Draw_BleConfig(void)
{
    u8g2_SetFont(&u8g2, u8g2_font_ncenB08_tr);
    u8g2_DrawStr(&u8g2, 8, 10, "Bluetooth Config");
    u8g2_DrawHLine(&u8g2, 0, 15, 128);

    char line[32];
    const char *status;
    if (!g_ble_connected)
        status = "DISCONNECTED";
    else if (g_show_passkey)
        status = "PAIRING";
    else if (g_is_bonded)
        status = "CONNECTED";
    else
        status = "NOT PAIRED";
    u8g2_DrawStr(&u8g2, 8, 30, status);

    char mac[18];
    Ble_Driver_GetMac(mac, sizeof(mac));
    u8g2_DrawStr(&u8g2, 8, 42, mac);

    if (g_show_passkey) {
        snprintf(line, sizeof(line), "Enter: %06" PRIu32, g_display_passkey);
    } else if (g_is_bonded) {
        snprintf(line, sizeof(line), "[CANC] Clear Bonds");
    } else {
        snprintf(line, sizeof(line), "Passkey: %06" PRIu32, g_display_passkey);
    }
    u8g2_DrawStr(&u8g2, 8, 54, line);
}
// --------------------------------------------------- Timer Pages -----------------------------------------------------

// ------------------------------------------------------ Driver -------------------------------------------------------
void Display_Sleep(bool sleep_en)
{
    u8g2_SetPowerSave(&u8g2, sleep_en ? 1 : 0);
}

void Display_Refresh(void)
{
    u8g2_ClearBuffer(&u8g2);

    switch (currentState) {
        case STATE_MAIN_SCREEN:
            Display_Draw_MainScreen();
            break;
        case STATE_PPG_RAW_6S_AVG:
            Display_Draw_PpgRaw6sAvg();
            break;
        case STATE_PPG_RAW_1S:
            Display_Draw_PpgRaw1s();
            break;
        case STATE_PPG_PROCESSED:
            Display_Draw_Processed();
            break;
        case STATE_PPG_HR:
            if (ppg_hr > 0 && ppg_hr != hr_last_stored) {
                hr_smoothed = 0.30f * (float)ppg_hr + 0.70f * hr_smoothed;
                uint8_t disp = (uint8_t)(hr_smoothed + 0.5f);
                if (disp < 1) disp = 1;
                hr_history[hr_history_head] = disp;
                hr_history_head = (hr_history_head + 1) % HR_HIST_SIZE;
                if (hr_history_count < HR_HIST_SIZE) hr_history_count++;
                hr_last_stored = ppg_hr;
            }
            Display_Draw_HR();
            break;
        case STATE_TIMER_SET:
            Display_Draw_TimerSet();
            break;
        case STATE_TIMER_RUNNING:
            Display_Draw_TimerRunning();
            break;
        case STATE_TIMER_DONE:
            Display_Draw_TimerDone();
            break;
        case STATE_BLE_CONFIG:
            Display_Draw_BleConfig();
            break;
        default:
            break;
    }

    u8g2_SendBuffer(&u8g2);

    frame_count++;
}
// ------------------------------------------------------ Driver -------------------------------------------------------