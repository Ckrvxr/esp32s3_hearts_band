#include "driver/gpio.h"
#include "esp_log.h"

#include "display.h"
// ------------------------------------------------------ Driver -------------------------------------------------------
#define KEY_UP      16
#define KEY_DOWN    15
#define KEY_CONFIRM  7
#define KEY_CANCEL   6

#define KEY_MASK    ((1ULL << KEY_UP) | (1ULL << KEY_DOWN) | (1ULL << KEY_CONFIRM) | (1ULL << KEY_CANCEL))

#define DEBOUNCE_THRESHOLD  3

static const char *TAG = "KEY";

static const uint8_t key_gpios[] = {KEY_UP, KEY_DOWN, KEY_CONFIRM, KEY_CANCEL};

static uint8_t debounce_cnt[4];
static uint8_t prev_state[4];
static uint8_t curr_state[4];

static void key_gpio_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = KEY_MASK,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
}
// ------------------------------------------------------ Driver -------------------------------------------------------

// --------------------------------------------------- Application -----------------------------------------------------
void Key_Scan(void)
{
    for (int i = 0; i < 4; i++) {
        curr_state[i] = (gpio_get_level(key_gpios[i]) == 0) ? 1 : 0;

        if (curr_state[i] == prev_state[i]) {
            if (debounce_cnt[i] < DEBOUNCE_THRESHOLD) {
                debounce_cnt[i]++;
            }
        } else {
            debounce_cnt[i] = 0;
        }

        prev_state[i] = curr_state[i];

        if (debounce_cnt[i] == DEBOUNCE_THRESHOLD && curr_state[i]) {
            debounce_cnt[i]++;

            switch (key_gpios[i]) {
                case KEY_UP:
                    ESP_LOGI(TAG, "Dectect UP key being pressed.");
                    break;
                case KEY_DOWN:
                    menu_index++;
                    ESP_LOGI(TAG, "Dectect DOWN key being pressed.");
                    break;
                case KEY_CONFIRM:
                    ESP_LOGI(TAG, "Dectect CONFIRM key being pressed.");
                    break;
                case KEY_CANCEL:
                    ESP_LOGI(TAG, "Dectect CANCEL key being pressed.");
                    break;
            }
        }
    }
}
// --------------------------------------------------- Application -----------------------------------------------------

// ------------------------------------------------------ Driver -------------------------------------------------------
void Key_Init(void)
{
    key_gpio_init();

    for (int i = 0; i < 4; i++) {
        prev_state[i] = 0;
        curr_state[i] = 0;
        debounce_cnt[i] = 0;
    }

    ESP_LOGI(TAG, "Key driver initialized");
}
// ------------------------------------------------------ Driver -------------------------------------------------------
