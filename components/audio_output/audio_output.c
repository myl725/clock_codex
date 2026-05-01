#include "audio_output.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "board_config.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "audio_output";
static const uint32_t AUDIO_OUTPUT_DMA_DESC_COUNT = 10;
static const uint32_t AUDIO_OUTPUT_DMA_FRAME_COUNT = 480;

static i2s_chan_handle_t s_tx_handle;
static bool s_initialized;
static bool s_started;
static uint32_t s_write_call_count;
static uint32_t s_sample_rate_hz = BOARD_AUDIO_OUTPUT_SAMPLE_RATE;

static esp_err_t audio_output_preload_bytes(const void *buffer, size_t byte_count)
{
    size_t bytes_loaded = 0;
    size_t total_loaded = 0;
    uint32_t preload_calls = 0;

    ESP_RETURN_ON_FALSE(buffer != NULL, ESP_ERR_INVALID_ARG, TAG, "preload buffer is null");
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "audio output not initialized");
    ESP_RETURN_ON_FALSE(!s_started, ESP_ERR_INVALID_STATE, TAG, "audio output already started");

    do {
        bytes_loaded = 0;
        ESP_RETURN_ON_ERROR(i2s_channel_preload_data(s_tx_handle, buffer, byte_count, &bytes_loaded), TAG, "failed to preload audio data");
        total_loaded += bytes_loaded;
        preload_calls++;
    } while (bytes_loaded == byte_count);

    ESP_LOGI(
        TAG,
        "audio preload complete: calls=%lu chunk=%u total=%u",
        (unsigned long)preload_calls,
        (unsigned)byte_count,
        (unsigned)total_loaded
    );
    return ESP_OK;
}

static esp_err_t audio_output_write_bytes(const void *buffer, size_t byte_count, uint32_t timeout_ms)
{
    size_t bytes_written = 0;
    esp_err_t err;

    ESP_RETURN_ON_FALSE(s_started, ESP_ERR_INVALID_STATE, TAG, "output not started");
    s_write_call_count++;
    err = i2s_channel_write(s_tx_handle, buffer, byte_count, &bytes_written, pdMS_TO_TICKS(timeout_ms));
    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "i2s write failed on call=%lu requested=%u written=%u timeout_ms=%lu err=%s",
            (unsigned long)s_write_call_count,
            (unsigned)byte_count,
            (unsigned)bytes_written,
            (unsigned long)timeout_ms,
            esp_err_to_name(err)
        );
        return err;
    }
    if (bytes_written != byte_count) {
        ESP_LOGE(
            TAG,
            "partial audio write on call=%lu requested=%u written=%u",
            (unsigned long)s_write_call_count,
            (unsigned)byte_count,
            (unsigned)bytes_written
        );
        return ESP_ERR_TIMEOUT;
    }
    if (s_write_call_count <= 4 || (s_write_call_count % 32U) == 0U) {
        ESP_LOGI(
            TAG,
            "i2s write ok call=%lu bytes=%u",
            (unsigned long)s_write_call_count,
            (unsigned)byte_count
        );
    }

    return ESP_OK;
}

esp_err_t audio_output_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "audio output already initialized");
        return ESP_OK;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(BOARD_AUDIO_I2S_NUM, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = AUDIO_OUTPUT_DMA_DESC_COUNT;
    chan_cfg.dma_frame_num = AUDIO_OUTPUT_DMA_FRAME_COUNT;
    chan_cfg.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_tx_handle, NULL), TAG, "failed to allocate I2S TX channel");

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(s_sample_rate_hz),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = BOARD_AUDIO_OUTPUT_BCLK_GPIO,
            .ws = BOARD_AUDIO_OUTPUT_WS_GPIO,
            .dout = BOARD_AUDIO_OUTPUT_DATA_GPIO,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx_handle, &std_cfg), TAG, "failed to init I2S std mode");

    s_initialized = true;
    s_write_call_count = 0;
    ESP_LOGI(
        TAG,
        "MAX98357A output ready: %lu Hz, BCLK=%d WS=%d DOUT=%d",
        (unsigned long)s_sample_rate_hz,
        BOARD_AUDIO_OUTPUT_BCLK_GPIO,
        BOARD_AUDIO_OUTPUT_WS_GPIO,
        BOARD_AUDIO_OUTPUT_DATA_GPIO
    );
    return ESP_OK;
}

esp_err_t audio_output_preload_stereo(const int16_t *samples, size_t frame_count)
{
    ESP_RETURN_ON_FALSE(samples != NULL, ESP_ERR_INVALID_ARG, TAG, "samples is null");
    return audio_output_preload_bytes(samples, frame_count * 2 * sizeof(int16_t));
}

esp_err_t audio_output_preload_silence(size_t frame_count)
{
    int16_t *zero_buffer = calloc(frame_count * 2, sizeof(int16_t));
    ESP_RETURN_ON_FALSE(zero_buffer != NULL, ESP_ERR_NO_MEM, TAG, "failed to allocate preload silence buffer");

    esp_err_t err = audio_output_preload_stereo(zero_buffer, frame_count);
    free(zero_buffer);
    return err;
}

esp_err_t audio_output_start(void)
{
    if (!s_initialized) {
        ESP_RETURN_ON_ERROR(audio_output_init(), TAG, "failed to init audio output");
    }

    if (s_started) {
        return ESP_OK;
    }

    s_write_call_count = 0;
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx_handle), TAG, "failed to enable I2S TX channel");
    s_started = true;
    ESP_LOGI(TAG, "audio output started");
    return ESP_OK;
}

esp_err_t audio_output_stop(void)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "audio output not initialized");
    if (!s_started) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(i2s_channel_disable(s_tx_handle), TAG, "failed to disable I2S TX channel");
    s_started = false;
    ESP_LOGI(TAG, "audio output stopped after calls=%lu", (unsigned long)s_write_call_count);
    return ESP_OK;
}

esp_err_t audio_output_write_stereo(const int16_t *samples, size_t frame_count, uint32_t timeout_ms)
{
    ESP_RETURN_ON_FALSE(samples != NULL, ESP_ERR_INVALID_ARG, TAG, "samples is null");
    return audio_output_write_bytes(samples, frame_count * 2 * sizeof(int16_t), timeout_ms);
}

esp_err_t audio_output_write_silence(size_t frame_count, uint32_t timeout_ms)
{
    int16_t *zero_buffer = calloc(frame_count * 2, sizeof(int16_t));
    ESP_RETURN_ON_FALSE(zero_buffer != NULL, ESP_ERR_NO_MEM, TAG, "failed to allocate silence buffer");

    esp_err_t err = audio_output_write_stereo(zero_buffer, frame_count, timeout_ms);
    free(zero_buffer);
    return err;
}

esp_err_t audio_output_set_sample_rate(uint32_t sample_rate_hz)
{
    ESP_RETURN_ON_FALSE(sample_rate_hz > 0U, ESP_ERR_INVALID_ARG, TAG, "sample_rate_hz must be positive");
    if (sample_rate_hz == s_sample_rate_hz) {
        return ESP_OK;
    }

    if (!s_initialized) {
        s_sample_rate_hz = sample_rate_hz;
        return ESP_OK;
    }

    ESP_RETURN_ON_FALSE(!s_started, ESP_ERR_INVALID_STATE, TAG, "cannot change sample rate while started");

    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate_hz);
    ESP_RETURN_ON_ERROR(i2s_channel_reconfig_std_clock(s_tx_handle, &clk_cfg), TAG, "failed to reconfig sample rate");
    s_sample_rate_hz = sample_rate_hz;
    ESP_LOGI(TAG, "audio output sample rate reconfigured to %lu Hz", (unsigned long)s_sample_rate_hz);
    return ESP_OK;
}

uint32_t audio_output_get_sample_rate(void)
{
    return s_sample_rate_hz;
}
