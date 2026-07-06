#include "esp_log.h"
#include "esp_check.h"
#include "driver/i2c_master.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "max30100.h"

#define I2C_PORT       I2C_NUM_1
#define I2C_SCL        18
#define I2C_SDA        8
#define I2C_FREQ_HZ    400000
#define I2C_TIMEOUT_MS 100

static const char *TAG = "MAX30100";

static i2c_master_bus_handle_t bus_handle;
static i2c_master_dev_handle_t dev_handle;

static uint8_t current_ir_idx  = MAX30100_AGC_INIT_CURRENT;
static uint8_t current_red_idx = MAX30100_AGC_RED_INIT_CURRENT;

// AGC 内部状态
static uint16_t agc_ir_buffer[MAX30100_AGC_WINDOW_SIZE];
static uint8_t  agc_buffer_idx = 0;
static uint8_t  agc_buffer_cnt = 0;

MAX30100_State_t g_max30100_state = MAX30100_STATE_NORMAL;

esp_err_t MAX30100_Init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_PORT,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &bus_handle), TAG, "bus create failed");

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MAX30100_I2C_ADDR,
        .scl_speed_hz = I2C_FREQ_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle), TAG, "add device failed");

    uint8_t part_id;
    ESP_RETURN_ON_ERROR(MAX30100_ReadReg(MAX30100_REG_PART_ID, &part_id), TAG, "read PART_ID failed");
    if (part_id != 0x11) {
        ESP_LOGE(TAG, "Invalid PART_ID: 0x%02X (expected 0x11)", part_id);
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "PART_ID = 0x%02X", part_id);

    MAX30100_WriteReg(MAX30100_REG_MODE_CONFIG, MAX30100_MODE_RESET);
    vTaskDelay(pdMS_TO_TICKS(10));

    MAX30100_WriteReg(MAX30100_REG_MODE_CONFIG, MAX30100_MODE_SPO2_HR);

    uint8_t spo2_cfg = (MAX30100_SAMPRATE_100HZ << 2) | MAX30100_PW_1600US_16BITS | MAX30100_HIRES_EN;
    MAX30100_WriteReg(MAX30100_REG_SPO2_CONFIG, spo2_cfg);

    MAX30100_WriteReg(MAX30100_REG_LED_CONFIG, (current_ir_idx << 4) | current_red_idx);

    MAX30100_WriteReg(MAX30100_REG_FIFO_WRITE_POINTER, 0);
    MAX30100_WriteReg(MAX30100_REG_FIFO_READ_POINTER, 0);
    MAX30100_WriteReg(MAX30100_REG_FIFO_OVERFLOW, 0);

    ESP_LOGI(TAG, "Initialized");
    return ESP_OK;
}

esp_err_t MAX30100_ReadReg(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(dev_handle, &reg, 1, val, 1, I2C_TIMEOUT_MS);
}

esp_err_t MAX30100_WriteReg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(dev_handle, buf, 2, I2C_TIMEOUT_MS);
}

uint8_t MAX30100_ReadFifo(uint16_t *ir, uint16_t *red)
{
    uint8_t wr_ptr, rd_ptr;
    if (MAX30100_ReadReg(MAX30100_REG_FIFO_WRITE_POINTER, &wr_ptr) != ESP_OK) return 0;
    if (MAX30100_ReadReg(MAX30100_REG_FIFO_READ_POINTER, &rd_ptr) != ESP_OK) return 0;

    uint8_t available = (wr_ptr - rd_ptr) & (MAX30100_FIFO_DEPTH - 1);
    if (available == 0) return 0;

    uint8_t buf[MAX30100_FIFO_DEPTH * 4];
    uint8_t reg = MAX30100_REG_FIFO_DATA;
    uint8_t len = available * 4;

    if (i2c_master_transmit_receive(dev_handle, &reg, 1, buf, len, I2C_TIMEOUT_MS) != ESP_OK) {
        return 0;
    }

    for (int i = 0; i < available; i++) {
        red[i] = ((uint16_t)buf[i * 4]     << 8) | buf[i * 4 + 1];
        ir[i]  = ((uint16_t)buf[i * 4 + 2] << 8) | buf[i * 4 + 3];
    }

    return available;
}

void MAX30100_AutoAdjust_Init(void)
{
    current_ir_idx  = MAX30100_AGC_INIT_CURRENT;
    current_red_idx = MAX30100_AGC_RED_INIT_CURRENT;

    agc_buffer_idx = 0;
    agc_buffer_cnt = 0;

    MAX30100_WriteReg(MAX30100_REG_LED_CONFIG,
                      (current_ir_idx << 4) | current_red_idx);
    // ESP_LOGI(TAG, "AGC init: IR=0x%X RED=0x%X", current_ir_idx, current_red_idx);
}

void MAX30100_AutoAdjust_FeedSample(uint16_t ir)
{
    agc_ir_buffer[agc_buffer_idx] = ir;
    agc_buffer_idx = (agc_buffer_idx + 1) % MAX30100_AGC_WINDOW_SIZE;
    if (agc_buffer_cnt < MAX30100_AGC_WINDOW_SIZE)
        agc_buffer_cnt++;
}

static uint16_t median_filter(uint16_t *buf, uint8_t cnt)
{
    uint16_t tmp[MAX30100_AGC_WINDOW_SIZE];
    for (uint8_t i = 0; i < cnt; i++)
        tmp[i] = buf[i];

    for (uint8_t i = 1; i < cnt; i++) {
        uint16_t key = tmp[i];
        int8_t j = i - 1;
        while (j >= 0 && tmp[j] > key) {
            tmp[j + 1] = tmp[j];
            j--;
        }
        tmp[j + 1] = key;
    }
    return tmp[cnt / 2];
}

void MAX30100_AutoAdjust_Run(float dcw_ir, float dcw_red, uint32_t now_ms)
{
    static uint32_t last_main_ms = 0;
    static uint32_t last_sub_ms  = 0;

    if (agc_buffer_cnt == 0) return;

    // ── 主环: IR 跟踪 @ 500ms ──
    if (now_ms - last_main_ms < MAX30100_AGC_MAIN_PERIOD_MS)
        return;
    last_main_ms = now_ms;

    uint16_t ir_med = median_filter(agc_ir_buffer, agc_buffer_cnt);

    if (ir_med < MAX30100_AGC_NO_SIGNAL_THR)
        return;

    if (ir_med >= MAX30100_AGC_SAT_THRESHOLD) {
        current_ir_idx = (current_ir_idx >= 3)
                         ? (current_ir_idx - 3) : 0;
        goto write_led;
    }

    if (dcw_red >= MAX30100_AGC_SAT_THRESHOLD) {
        current_red_idx = (current_red_idx >= 3)
                          ? (current_red_idx - 3) : 0;
        goto write_led;
    }

    int32_t e = (int32_t)ir_med - MAX30100_AGC_TARGET_CENTER;
    int8_t step = 0;

    if      (e < -(int32_t)MAX30100_AGC_STEP_THRESHOLD) step = 2;
    else if (e < -(int32_t)MAX30100_AGC_DEAD_ZONE_LOW)  step = 1;
    else if (e <= (int32_t)MAX30100_AGC_DEAD_ZONE_HIGH) step = 0;
    else if (e <= (int32_t)MAX30100_AGC_STEP_THRESHOLD) step = -1;
    else                                                 step = -2;

    if (step != 0) {
        int16_t new_ir = (int16_t)current_ir_idx + step;
        if (new_ir < 0)  new_ir = 0;
        if (new_ir > 15) new_ir = 15;
        current_ir_idx = (uint8_t)new_ir;
    }

write_led:
    MAX30100_WriteReg(MAX30100_REG_LED_CONFIG,
                      (current_ir_idx << 4) | current_red_idx);

    // ── 副环: RED 平衡 @ 1000ms ──
    if (now_ms - last_sub_ms < MAX30100_AGC_SUB_PERIOD_MS)
        return;
    last_sub_ms = now_ms;

    if (dcw_ir < 1.0f) return;

    float ratio = dcw_red / dcw_ir;
    int8_t red_step = 0;
    if      (ratio < MAX30100_AGC_RED_RATIO_LOW)  red_step = 1;
    else if (ratio > MAX30100_AGC_RED_RATIO_HIGH) red_step = -1;

    if (red_step != 0) {
        int16_t new_red = (int16_t)current_red_idx + red_step;
        if (new_red < 0)  new_red = 0;
        if (new_red > 15) new_red = 15;
        current_red_idx = (uint8_t)new_red;
        MAX30100_WriteReg(MAX30100_REG_LED_CONFIG,
                          (current_ir_idx << 4) | current_red_idx);
    }
}

void MAX30100_Sleep(void)
{
    uint8_t mode;
    if (MAX30100_ReadReg(MAX30100_REG_MODE_CONFIG, &mode) != ESP_OK) return;
    mode |= MAX30100_MODE_SHDN;
    MAX30100_WriteReg(MAX30100_REG_MODE_CONFIG, mode);
    g_max30100_state = MAX30100_STATE_SLEEPING;
    // ESP_LOGI(TAG, "Sleeping");
}

void MAX30100_Wake(void)
{
    MAX30100_WriteReg(MAX30100_REG_MODE_CONFIG, MAX30100_MODE_SPO2_HR);
    vTaskDelay(pdMS_TO_TICKS(10));
    MAX30100_WriteReg(MAX30100_REG_FIFO_WRITE_POINTER, 0);
    MAX30100_WriteReg(MAX30100_REG_FIFO_READ_POINTER, 0);
    g_max30100_state = MAX30100_STATE_NORMAL;
    // ESP_LOGI(TAG, "Woken");
}
