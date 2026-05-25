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

    MAX30100_WriteReg(MAX30100_REG_LED_CONFIG, (0x07 << 4) | 0x04);

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
        ir[i]  = ((uint16_t)buf[i * 4]     << 8) | buf[i * 4 + 1];
        red[i] = ((uint16_t)buf[i * 4 + 2] << 8) | buf[i * 4 + 3];

        ESP_LOGI(TAG, "IR=%5u  RED=%5u", ir[i], red[i]);
    }

    return available;
}
