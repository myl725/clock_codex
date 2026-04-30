#include "audio_input.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "driver/i2s_std.h"
#include "board_config.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "audio_input";
static const char *AUDIO_INPUT_FORMAT = "MN";
static const int AUDIO_INPUT_CHANNELS = 2;

static i2s_chan_handle_t s_rx_handle;
static int32_t *s_raw_buffer;
static size_t s_raw_capacity_words;
static bool s_initialized;

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
    int32_t scaled = raw_sample >> 14;
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

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(BOARD_AUDIO_I2S_NUM, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, NULL, &s_rx_handle), TAG, "failed to create I2S RX channel");

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(BOARD_AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = BOARD_AUDIO_BCLK_GPIO,
            .ws = BOARD_AUDIO_WS_GPIO,
            .dout = I2S_GPIO_UNUSED,
            .din = BOARD_AUDIO_DATA_IN_GPIO,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_rx_handle, &std_cfg), TAG, "failed to init I2S standard RX mode");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_rx_handle), TAG, "failed to enable I2S RX channel");

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
    const size_t raw_word_count = frame_count * 2;
    ESP_RETURN_ON_ERROR(audio_input_ensure_raw_capacity(raw_word_count), TAG, "failed to resize raw buffer");

    size_t bytes_read = 0;
    ESP_RETURN_ON_ERROR(
        i2s_channel_read(s_rx_handle, s_raw_buffer, raw_word_count * sizeof(int32_t), &bytes_read, portMAX_DELAY),
        TAG,
        "failed to read I2S microphone data"
    );

    const size_t raw_words_read = bytes_read / sizeof(int32_t);
    const size_t frames_read = raw_words_read / 2;
    const size_t mic_slot_index = BOARD_AUDIO_MIC_USE_LEFT_SLOT ? 0 : 1;

    for (size_t frame = 0; frame < frames_read; frame++) {
        int32_t raw_mic = s_raw_buffer[(frame * 2) + mic_slot_index];
        buffer[frame * AUDIO_INPUT_CHANNELS] = audio_input_convert_sample(raw_mic);
        buffer[(frame * AUDIO_INPUT_CHANNELS) + 1] = 0;
    }

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
