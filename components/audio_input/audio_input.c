#include "audio_input.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "i2s.h"
#include "board_config.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "audio_input";
static const char *AUDIO_INPUT_FORMAT = "MN";
static const int AUDIO_INPUT_CHANNELS = 2;

static int32_t *s_raw_buffer;
static size_t s_raw_capacity_words;
static bool s_initialized;
static uint16_t s_last_left_peak;
static uint16_t s_last_right_peak;
static uint32_t s_last_raw_peak;

static esp_err_t audio_input_ensure_raw_capacity(size_t raw_word_count)
{
    if (raw_word_count <= s_raw_capacity_words) {
        return ESP_OK;
    }

    int32_t *new_buffer = realloc(s_raw_buffer, raw_word_count * sizeof(int32_t));
    ESP_RETURN_ON_FALSE(new_buffer != NULL, ESP_ERR_NO_MEM, TAG, "failed to allocate raw audio buffer");

    s_raw_buffer = new_buffer;
    s_raw_capacity_words = raw_word_count;
    return ESP_OK;
}

static int16_t audio_input_convert_sample(int32_t raw_sample)
{
    int32_t scaled = raw_sample >> 8;
    if (scaled > INT16_MAX) {
        scaled = INT16_MAX;
    } else if (scaled < INT16_MIN) {
        scaled = INT16_MIN;
    }

    return (int16_t)scaled;
}

esp_err_t audio_input_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "audio input already initialized");
        return ESP_OK;
    }

    const i2s_config_t i2s_cfg = {
        .mode = I2S_MODE_MASTER | I2S_MODE_RX,
        .sample_rate = BOARD_AUDIO_SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = BOARD_AUDIO_MIC_USE_LEFT_SLOT ? I2S_CHANNEL_FMT_ONLY_LEFT : I2S_CHANNEL_FMT_ONLY_RIGHT,
        .communication_format = I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 256,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0,
    };

    const i2s_pin_config_t pin_cfg = {
        .bck_io_num = BOARD_AUDIO_BCLK_GPIO,
        .ws_io_num = BOARD_AUDIO_WS_GPIO,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = BOARD_AUDIO_DATA_IN_GPIO,
        .mck_io_num = I2S_PIN_NO_CHANGE,
    };

    ESP_RETURN_ON_ERROR(i2s_driver_install(BOARD_AUDIO_I2S_NUM, &i2s_cfg, 0, NULL), TAG, "failed to install I2S RX driver");
    ESP_RETURN_ON_ERROR(i2s_set_pin(BOARD_AUDIO_I2S_NUM, &pin_cfg), TAG, "failed to set I2S pins");
    ESP_RETURN_ON_ERROR(i2s_zero_dma_buffer(BOARD_AUDIO_I2S_NUM), TAG, "failed to clear I2S DMA buffer");

    s_initialized = true;
    ESP_LOGI(
        TAG,
        "INMP441 audio input ready on BCLK=%d WS=%d DIN=%d",
        BOARD_AUDIO_BCLK_GPIO,
        BOARD_AUDIO_WS_GPIO,
        BOARD_AUDIO_DATA_IN_GPIO
    );

    return ESP_OK;
}

esp_err_t audio_input_read(int16_t *buffer, size_t sample_count, size_t *samples_read)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "audio input not initialized");
    ESP_RETURN_ON_FALSE(buffer != NULL, ESP_ERR_INVALID_ARG, TAG, "buffer is null");
    ESP_RETURN_ON_FALSE((sample_count % AUDIO_INPUT_CHANNELS) == 0, ESP_ERR_INVALID_ARG, TAG, "sample count must be divisible by channel count");

    const size_t frame_count = sample_count / AUDIO_INPUT_CHANNELS;
    const size_t raw_word_count = frame_count;
    ESP_RETURN_ON_ERROR(audio_input_ensure_raw_capacity(raw_word_count), TAG, "failed to resize raw buffer");

    size_t bytes_read = 0;
    ESP_RETURN_ON_ERROR(
        i2s_read(BOARD_AUDIO_I2S_NUM, s_raw_buffer, raw_word_count * sizeof(int32_t), &bytes_read, portMAX_DELAY),
        TAG,
        "failed to read I2S microphone data"
    );

    const size_t raw_words_read = bytes_read / sizeof(int32_t);
    const size_t frames_read = raw_words_read;
    uint16_t left_peak = 0;
    uint16_t right_peak = 0;
    uint32_t raw_peak = 0;

    for (size_t frame = 0; frame < frames_read; frame++) {
        int32_t raw_mic = s_raw_buffer[frame];
        int16_t mic_sample = audio_input_convert_sample(raw_mic);
        uint32_t raw_abs = (uint32_t)(raw_mic < 0 ? -raw_mic : raw_mic);
        uint16_t mic_abs = (uint16_t)((raw_abs >> 8) > UINT16_MAX ? UINT16_MAX : (raw_abs >> 8));

        if (raw_abs > raw_peak) {
            raw_peak = raw_abs;
        }

        if (BOARD_AUDIO_MIC_USE_LEFT_SLOT) {
            if (mic_abs > left_peak) {
                left_peak = mic_abs;
            }
        } else {
            if (mic_abs > right_peak) {
                right_peak = mic_abs;
            }
        }

        buffer[frame * AUDIO_INPUT_CHANNELS] = mic_sample;
        buffer[(frame * AUDIO_INPUT_CHANNELS) + 1] = 0;
    }

    s_last_left_peak = left_peak;
    s_last_right_peak = right_peak;
    s_last_raw_peak = raw_peak;

    if (frames_read < frame_count) {
        memset(
            &buffer[frames_read * AUDIO_INPUT_CHANNELS],
            0,
            (frame_count - frames_read) * AUDIO_INPUT_CHANNELS * sizeof(int16_t)
        );
    }

    if (samples_read != NULL) {
        *samples_read = frames_read * AUDIO_INPUT_CHANNELS;
    }

    return ESP_OK;
}

void audio_input_get_debug_peaks(uint16_t *left_peak, uint16_t *right_peak)
{
    if (left_peak != NULL) {
        *left_peak = s_last_left_peak;
    }
    if (right_peak != NULL) {
        *right_peak = s_last_right_peak;
    }
}

uint32_t audio_input_get_debug_raw_peak(void)
{
    return s_last_raw_peak;
}

uint32_t audio_input_get_sample_rate(void)
{
    return BOARD_AUDIO_SAMPLE_RATE;
}

int audio_input_get_feed_channel_count(void)
{
    return AUDIO_INPUT_CHANNELS;
}

const char *audio_input_get_input_format(void)
{
    return AUDIO_INPUT_FORMAT;
}
