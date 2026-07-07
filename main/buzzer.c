#include "esp_log.h"
#include "driver/ledc.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "buzzer.h"

#define BUZZER_LEDC_MODE       LEDC_LOW_SPEED_MODE
#define BUZZER_LEDC_TIMER      LEDC_TIMER_0
#define BUZZER_LEDC_CHANNEL    LEDC_CHANNEL_0
#define BUZZER_DUTY_RES        LEDC_TIMER_8_BIT
#define BUZZER_DUTY_HALF       (128)
#define BUZZER_DUTY_MAX        (255)

static const char *TAG = "BUZZER";

static bool g_initialized = false;

static esp_err_t buzzer_set_freq(uint16_t freq_hz)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode = BUZZER_LEDC_MODE,
        .duty_resolution = BUZZER_DUTY_RES,
        .timer_num = BUZZER_LEDC_TIMER,
        .freq_hz = freq_hz,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    return ledc_timer_config(&timer_cfg);
}

void Buzzer_Init(void)
{
    ESP_ERROR_CHECK(buzzer_set_freq(440));

    ledc_channel_config_t chan_cfg = {
        .speed_mode = BUZZER_LEDC_MODE,
        .channel = BUZZER_LEDC_CHANNEL,
        .timer_sel = BUZZER_LEDC_TIMER,
        .gpio_num = BUZZER_PWM_PIN,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&chan_cfg));

    ledc_stop(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, 1);

    g_initialized = true;
    ESP_LOGI(TAG, "Initialized on GPIO%d (stopped, idle HIGH)", BUZZER_PWM_PIN);
}

void Buzzer_On(uint16_t freq_hz)
{
    if (freq_hz == 0 || !g_initialized) {
        Buzzer_Off();
        return;
    }

    esp_err_t ret = buzzer_set_freq(freq_hz);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "set_freq(%u) failed: %s", freq_hz, esp_err_to_name(ret));
        return;
    }

    ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, BUZZER_DUTY_HALF);
    ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL);
    ESP_LOGD(TAG, "ON  freq=%u Hz", freq_hz);
}

void Buzzer_Off(void)
{
    if (!g_initialized) return;
    ledc_stop(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, 1);
    ESP_LOGD(TAG, "OFF");
}

void Buzzer_PlayMelody(const BuzzerNote_t *notes, uint16_t count)
{
    if (!notes || count == 0) {
        ESP_LOGW(TAG, "PlayMelody: empty notes");
        return;
    }

    ESP_LOGI(TAG, "PlayMelody: %u notes", count);
    for (uint16_t i = 0; i < count; i++) {
        if (notes[i].freq == NOTE_REST) {
            Buzzer_Off();
        } else {
            Buzzer_On(notes[i].freq);
        }
        vTaskDelay(pdMS_TO_TICKS(notes[i].duration_ms));
    }
    Buzzer_Off();
    ESP_LOGI(TAG, "PlayMelody: done");
}

void Buzzer_PlayMoogCity(void)
{
    ESP_LOGI(TAG, "PlayMoogCity: starting");
    // Transposed up 1 semitone: Em → Fm
    static const BuzzerNote_t bar[] = {
        {NOTE_F4, 227}, {NOTE_C5, 227}, {NOTE_F5, 227}, {NOTE_GS5, 227},
        {NOTE_C5, 227}, {NOTE_DS5, 227}, {NOTE_GS5, 227}, {NOTE_C7, 227},
    };

    for (int i = 0; i < 8; i++) {
        Buzzer_PlayMelody(bar, sizeof(bar) / sizeof(bar[0]));
    }
    ESP_LOGI(TAG, "PlayMoogCity: done");
}

void Buzzer_PlayBachBWV847(void)
{
    ESP_LOGI(TAG, "PlayBachBWV847: starting");
    // BWV 847 bars 1-8, all notes ≥ C4
    static const BuzzerNote_t melody[] = {
        // Bar 1 - Cm
        {NOTE_C4, 200}, {NOTE_G4, 200}, {NOTE_C5, 200}, {NOTE_DS5, 200},
        {NOTE_G5, 200}, {NOTE_DS5, 200}, {NOTE_C5, 200}, {NOTE_G4, 200},
        // Bar 2 - Fm
        {NOTE_C4, 200}, {NOTE_GS4, 200}, {NOTE_C5, 200}, {NOTE_F5, 200},
        {NOTE_GS5, 200}, {NOTE_F5, 200}, {NOTE_C5, 200}, {NOTE_GS4, 200},
        // Bar 3 - G7
        {NOTE_B4, 200}, {NOTE_G4, 200}, {NOTE_B4, 200}, {NOTE_D5, 200},
        {NOTE_G5, 200}, {NOTE_D5, 200}, {NOTE_B4, 200}, {NOTE_G4, 200},
        // Bar 4 - Cm
        {NOTE_C4, 200}, {NOTE_G4, 200}, {NOTE_C5, 200}, {NOTE_DS5, 200},
        {NOTE_G5, 200}, {NOTE_DS5, 200}, {NOTE_C5, 200}, {NOTE_G4, 200},
        // Bar 5 - Ab
        {NOTE_C4, 200}, {NOTE_GS4, 200}, {NOTE_C5, 200}, {NOTE_DS5, 200},
        {NOTE_GS5, 200}, {NOTE_DS5, 200}, {NOTE_C5, 200}, {NOTE_GS4, 200},
        // Bar 6 - D7/F# (transposed up 1 octave)
        {NOTE_FS4, 200}, {NOTE_A4, 200}, {NOTE_C5, 200}, {NOTE_FS5, 200},
        {NOTE_A5, 200}, {NOTE_FS5, 200}, {NOTE_C5, 200}, {NOTE_A4, 200},
        // Bar 7 - G7 (transposed up 1 octave)
        {NOTE_G4, 200}, {NOTE_B4, 200}, {NOTE_D5, 200}, {NOTE_G5, 200},
        {NOTE_B5, 200}, {NOTE_G5, 200}, {NOTE_D5, 200}, {NOTE_B4, 200},
        // Bar 8 - Cm (resolution)
        {NOTE_C4, 200}, {NOTE_G4, 200}, {NOTE_C5, 200}, {NOTE_DS5, 200},
        {NOTE_G5, 200}, {NOTE_DS5, 200}, {NOTE_C5, 200}, {NOTE_G4, 200},
    };
    Buzzer_PlayMelody(melody, sizeof(melody) / sizeof(melody[0]));
    ESP_LOGI(TAG, "PlayBachBWV847: done");
}

void Buzzer_PlayScale(void)
{
    ESP_LOGI(TAG, "PlayScale: starting");
    static const BuzzerNote_t scale[] = {
        {NOTE_C4, 400}, {NOTE_D4, 400}, {NOTE_E4, 400}, {NOTE_F4, 400},
        {NOTE_G4, 400}, {NOTE_A4, 400}, {NOTE_B4, 400}, {NOTE_C5, 400},
        {NOTE_D5, 400}, {NOTE_E5, 400}, {NOTE_F5, 400}, {NOTE_G5, 400},
        {NOTE_A5, 400}, {NOTE_B5, 400}, {NOTE_C6, 800}, {NOTE_REST, 400},
        {NOTE_B5, 400}, {NOTE_A5, 400}, {NOTE_G5, 400}, {NOTE_F5, 400},
        {NOTE_E5, 400}, {NOTE_D5, 400}, {NOTE_C5, 400}, {NOTE_B4, 400},
        {NOTE_A4, 400}, {NOTE_G4, 400}, {NOTE_F4, 400}, {NOTE_E4, 400},
        {NOTE_D4, 400}, {NOTE_C4, 800},
    };
    Buzzer_PlayMelody(scale, sizeof(scale) / sizeof(scale[0]));
    ESP_LOGI(TAG, "PlayScale: done");
}

void Buzzer_TestTone(void)
{
    ESP_LOGI(TAG, "TestTone: 2200Hz 3s");
    Buzzer_On(2200);
    vTaskDelay(pdMS_TO_TICKS(3000));
    Buzzer_Off();
    ESP_LOGI(TAG, "TestTone: done");
}


