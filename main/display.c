#include <string.h>

#include "esp_log.h"
#include "driver/i2c_master.h"
#include "rom/ets_sys.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "u8g2.h"

#include "display.h"

// ------------------------------------------------------ Driver -------------------------------------------------------
#define I2C_MASTER_SCL      4
#define I2C_MASTER_SDA      5
#define I2C_MASTER_FREQ_HZ  400000
#define I2C_ADDR_7BIT       0x3C
#define I2C_MASTER_TIMEOUT_MS 100

static const char *TAG = "DISPLAY";

volatile DisplayState_t currentState = STATE_MAIN_SCREEN;
volatile uint8_t menu_index = 1;
volatile uint8_t slect_index = 0;

static u8g2_t u8g2;
static uint32_t frame_count = 0;
static i2c_master_bus_handle_t bus_handle;
static i2c_master_dev_handle_t dev_handle;
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

    ESP_LOGI(TAG, "Display initialized");
}
// ------------------------------------------------------ Driver -------------------------------------------------------

// --------------------------------------------------- Application -----------------------------------------------------
static void Display_Draw_LiveAnimation(int x, int y)
{
    uint8_t phase = (frame_count / 2) % 4;
    int8_t dx = (phase == 1 || phase == 2) ? 3 : 0;
    int8_t dy = (phase == 2 || phase == 3) ? 3 : 0;
    u8g2_DrawBox(&u8g2, x + dx, y + dy, 2, 2);
}

static void __attribute__((unused)) Display_Draw_Cursor(uint8_t y, uint8_t is_selected, uint8_t is_editing)
{
    if (!is_selected) return;

    if (is_editing) {
        if ((frame_count / 4) % 2) {
            u8g2_DrawStr(&u8g2, 2, y, "*");
        }
    } else {
        uint8_t offset = (frame_count / 4) % 2;
        u8g2_DrawStr(&u8g2, offset, y, ">");
    }
}

static void Display_Draw_MainScreen(void)
{
    u8g2_SetFont(&u8g2, u8g2_font_ncenB08_tr);
    u8g2_DrawStr(&u8g2, 8, 10, "Hearts Band");
    Display_Draw_LiveAnimation(108, 2);

    u8g2_DrawHLine(&u8g2, 0, 14, 128);

    u8g2_DrawStr(&u8g2, 8, 32, "Hello World !");
}
// --------------------------------------------------- Application -----------------------------------------------------


// ------------------------------------------------------ Driver -------------------------------------------------------
void Display_Refresh(void)
{
    u8g2_ClearBuffer(&u8g2);

    switch (currentState) {
        case STATE_MAIN_SCREEN:
            Display_Draw_MainScreen();
            break;
        default:
            break;
    }

    u8g2_SendBuffer(&u8g2);

    frame_count++;
}
// ------------------------------------------------------ Driver -------------------------------------------------------