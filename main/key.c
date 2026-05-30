#include "driver/gpio.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "key.h"
#include "display.h"
#include "timer.h"

// ------------------------------------------------------ Driver -------------------------------------------------------
#define KEY_UP      16
#define KEY_DOWN    15
#define KEY_CONFIRM  7
#define KEY_CANCEL   6

#define KEY_MASK    ((1ULL << KEY_UP) | (1ULL << KEY_DOWN) | (1ULL << KEY_CONFIRM) | (1ULL << KEY_CANCEL))

#define DEBOUNCE_THRESHOLD     3
#define LONG_PRESS_MS          1000
#define DOUBLE_CLICK_WINDOW_MS 400

static const char *TAG = "KEY";

typedef enum {
    KEY_STATE_IDLE,
    KEY_STATE_PRESSED,
    KEY_STATE_WAIT_DOUBLE,
    KEY_STATE_WAIT_RELEASE,
} key_state_t;

typedef struct {
    uint8_t gpio;
    uint8_t state;
    uint8_t curr;
    uint8_t prev;
    uint8_t cnt;
    uint8_t long_press_sent;
    TickType_t tick;
} key_ctx_t;

static key_ctx_t key_ctx[4] = {
    { .gpio = KEY_UP,      .state = KEY_STATE_IDLE, .curr = 0, .prev = 0, .cnt = 0, .long_press_sent = 0, .tick = 0 },
    { .gpio = KEY_DOWN,    .state = KEY_STATE_IDLE, .curr = 0, .prev = 0, .cnt = 0, .long_press_sent = 0, .tick = 0 },
    { .gpio = KEY_CONFIRM, .state = KEY_STATE_IDLE, .curr = 0, .prev = 0, .cnt = 0, .long_press_sent = 0, .tick = 0 },
    { .gpio = KEY_CANCEL,  .state = KEY_STATE_IDLE, .curr = 0, .prev = 0, .cnt = 0, .long_press_sent = 0, .tick = 0 },
};

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
KeyEvent_t Key_Scan(uint8_t *out_key)
{
    for (int i = 0; i < 4; i++) {
        key_ctx_t *k = &key_ctx[i];

        k->curr = (gpio_get_level(k->gpio) == 0);

        if (k->curr == k->prev) {
            if (k->cnt < DEBOUNCE_THRESHOLD) k->cnt++;
        } else {
            k->cnt = 0;
        }
        k->prev = k->curr;

        if (k->cnt < DEBOUNCE_THRESHOLD) continue;

        TickType_t now = xTaskGetTickCount();

        switch (k->state) {
            case KEY_STATE_IDLE:
                if (k->curr) {
                    k->state = KEY_STATE_PRESSED;
                    k->tick = now;
                    k->long_press_sent = 0;
                }
                break;

            case KEY_STATE_PRESSED:
                if (!k->curr) {
                    if (k->long_press_sent) {
                        k->state = KEY_STATE_WAIT_RELEASE;
                    } else {
                        k->state = KEY_STATE_WAIT_DOUBLE;
                        k->tick = now;
                    }
                } else if (!k->long_press_sent &&
                           (now - k->tick) >= pdMS_TO_TICKS(LONG_PRESS_MS)) {
                    k->long_press_sent = 1;
                    *out_key = i;
                    return KEY_EVENT_LONG_PRESS;
                }
                break;

            case KEY_STATE_WAIT_DOUBLE:
                if (k->curr) {
                    k->state = KEY_STATE_WAIT_RELEASE;
                    *out_key = i;
                    return KEY_EVENT_DOUBLE_CLICK;
                } else if ((now - k->tick) >= pdMS_TO_TICKS(DOUBLE_CLICK_WINDOW_MS)) {
                    k->state = KEY_STATE_IDLE;
                    *out_key = i;
                    return KEY_EVENT_CLICK;
                }
                break;

            case KEY_STATE_WAIT_RELEASE:
                if (!k->curr) {
                    k->state = KEY_STATE_IDLE;
                }
                break;
        }
    }

    return KEY_EVENT_NONE;
}

void Key_Event_Handler(KeyEvent_t evt, uint8_t key_id)
{
    if (currentState == STATE_TIMER_SET || currentState == STATE_TIMER_DONE) {
        if (currentState == STATE_TIMER_DONE) {
            Timer_ClearExpiryFlag(TIMER_DRINK);
            Timer_ClearExpiryFlag(TIMER_MEDICINE);
        }
        currentState = STATE_TIMER_STATUS;
        return;
    }

    switch (key_id) {
        case KEY_IDX_UP:
            switch (evt) {
                case KEY_EVENT_CLICK:
                    ESP_LOGI(TAG, "UP CLICK");
                    do {
                        currentState = (currentState + 1) % STATE_COUNT;
                    } while (!DisplayState_IsVisible(currentState));
                    break;
                case KEY_EVENT_DOUBLE_CLICK: ESP_LOGI(TAG, "UP DOUBLE_CLICK"); break;
                case KEY_EVENT_LONG_PRESS:   ESP_LOGI(TAG, "UP LONG_PRESS"); break;
                default: break;
            }
            break;
        case KEY_IDX_DOWN:
            switch (evt) {
                case KEY_EVENT_CLICK:
                    ESP_LOGI(TAG, "DOWN CLICK");
                    do {
                        currentState = (currentState - 1 + STATE_COUNT) % STATE_COUNT;
                    } while (!DisplayState_IsVisible(currentState));
                    break;
                case KEY_EVENT_DOUBLE_CLICK: ESP_LOGI(TAG, "DOWN DOUBLE_CLICK"); break;
                case KEY_EVENT_LONG_PRESS:   ESP_LOGI(TAG, "DOWN LONG_PRESS"); break;
                default: break;
            }
            break;
        case KEY_IDX_CONFIRM:
            switch (evt) {
                case KEY_EVENT_CLICK:
                    if (currentState == STATE_TIMER_DONE) {
                        currentState = STATE_TIMER_STATUS;
                    }
                    ESP_LOGI(TAG, "CONFIRM CLICK");
                    break;
                case KEY_EVENT_DOUBLE_CLICK: ESP_LOGI(TAG, "CONFIRM DOUBLE_CLICK"); break;
                case KEY_EVENT_LONG_PRESS:   ESP_LOGI(TAG, "CONFIRM LONG_PRESS"); break;
                default: break;
            }
            break;
        case KEY_IDX_CANCEL:
            switch (evt) {
                case KEY_EVENT_CLICK:        ESP_LOGI(TAG, "CANCEL CLICK"); break;
                case KEY_EVENT_DOUBLE_CLICK: ESP_LOGI(TAG, "CANCEL DOUBLE_CLICK"); break;
                case KEY_EVENT_LONG_PRESS:
                    ESP_LOGI(TAG, "CANCEL LONG_PRESS");
                    break;
                default: break;
            }
            break;
    }
}
// --------------------------------------------------- Application -----------------------------------------------------

// ------------------------------------------------------ Driver -------------------------------------------------------
void Key_Init(void)
{
    key_gpio_init();
    ESP_LOGI(TAG, "Key driver initialized");
}
// ------------------------------------------------------ Driver -------------------------------------------------------
