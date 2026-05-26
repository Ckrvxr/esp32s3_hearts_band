#pragma once

#include <stdint.h>
#include "esp_err.h"

#define MAX30100_I2C_ADDR               0x57
#define MAX30100_FIFO_DEPTH             16

#define MAX30100_REG_INTERRUPT_STATUS   0x00
#define MAX30100_REG_INTERRUPT_ENABLE   0x01
#define MAX30100_REG_FIFO_WRITE_POINTER 0x02
#define MAX30100_REG_FIFO_OVERFLOW      0x03
#define MAX30100_REG_FIFO_READ_POINTER  0x04
#define MAX30100_REG_FIFO_DATA          0x05
#define MAX30100_REG_MODE_CONFIG        0x06
#define MAX30100_REG_SPO2_CONFIG        0x07
#define MAX30100_REG_LED_CONFIG         0x09
#define MAX30100_REG_TEMP_INT           0x16
#define MAX30100_REG_TEMP_FRAC          0x17
#define MAX30100_REG_REVISION_ID        0xFE
#define MAX30100_REG_PART_ID            0xFF

#define MAX30100_MODE_HR_ONLY           0x02
#define MAX30100_MODE_SPO2_HR           0x03
#define MAX30100_MODE_RESET             (1 << 6)
#define MAX30100_MODE_SHDN              (1 << 7)

#define MAX30100_IDLE_THRESHOLD_IR      500
#define MAX30100_IDLE_THRESHOLD_RED     2500
#define MAX30100_IDLE_TIMEOUT_MS        5000
#define MAX30100_WAKE_INTERVAL_MS       1000

typedef enum {
    MAX30100_STATE_NORMAL,
    MAX30100_STATE_IDLE,
    MAX30100_STATE_SLEEPING,
} MAX30100_State_t;

extern MAX30100_State_t g_max30100_state;

#define MAX30100_SAMPRATE_50HZ          0x00
#define MAX30100_SAMPRATE_100HZ         0x01
#define MAX30100_SAMPRATE_167HZ         0x02
#define MAX30100_SAMPRATE_200HZ         0x03
#define MAX30100_SAMPRATE_400HZ         0x04
#define MAX30100_SAMPRATE_600HZ         0x05
#define MAX30100_SAMPRATE_800HZ         0x06
#define MAX30100_SAMPRATE_1000HZ        0x07

#define MAX30100_PW_200US_13BITS        0x00
#define MAX30100_PW_400US_14BITS        0x01
#define MAX30100_PW_800US_15BITS        0x02
#define MAX30100_PW_1600US_16BITS       0x03

#define MAX30100_HIRES_EN               (1 << 6)

#define MAX30100_LED_CURR_0MA           0x00
#define MAX30100_LED_CURR_4_4MA         0x01
#define MAX30100_LED_CURR_7_6MA         0x02
#define MAX30100_LED_CURR_11MA          0x03
#define MAX30100_LED_CURR_14_2MA        0x04
#define MAX30100_LED_CURR_17_4MA        0x05
#define MAX30100_LED_CURR_20_8MA        0x06
#define MAX30100_LED_CURR_24MA          0x07
#define MAX30100_LED_CURR_27_1MA        0x08
#define MAX30100_LED_CURR_30_6MA        0x09
#define MAX30100_LED_CURR_33_8MA        0x0A
#define MAX30100_LED_CURR_37MA          0x0B
#define MAX30100_LED_CURR_40_2MA        0x0C
#define MAX30100_LED_CURR_43_6MA        0x0D
#define MAX30100_LED_CURR_46_8MA        0x0E
#define MAX30100_LED_CURR_50MA          0x0F

// === AGC 改进算法参数 ===
#define MAX30100_AGC_WINDOW_SIZE        8
#define MAX30100_AGC_TARGET_CENTER      35000
#define MAX30100_AGC_DEAD_ZONE_LOW      25000
#define MAX30100_AGC_DEAD_ZONE_HIGH     45000
#define MAX30100_AGC_STEP_THRESHOLD     15000
#define MAX30100_AGC_SAT_THRESHOLD      65500
#define MAX30100_AGC_NO_SIGNAL_THR      100
#define MAX30100_AGC_INIT_CURRENT       0x08
#define MAX30100_AGC_MAIN_PERIOD_MS     500
#define MAX30100_AGC_SUB_PERIOD_MS      1000
#define MAX30100_AGC_RED_RATIO_LOW      0.8f
#define MAX30100_AGC_RED_RATIO_HIGH     1.2f

esp_err_t MAX30100_Init(void);
void     MAX30100_AutoAdjust_Init(void);
void     MAX30100_AutoAdjust_FeedSample(uint16_t ir);
void     MAX30100_AutoAdjust_Run(float dcw_ir, float dcw_red, uint32_t now_ms);
void     MAX30100_Sleep(void);
void     MAX30100_Wake(void);
esp_err_t MAX30100_ReadReg(uint8_t reg, uint8_t *val);
esp_err_t MAX30100_WriteReg(uint8_t reg, uint8_t val);
uint8_t   MAX30100_ReadFifo(uint16_t *ir, uint16_t *red);
