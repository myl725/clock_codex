#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2s.h"

static const char *TAG = "inmp441_min";

#define MIC_I2S_PORT I2S_NUM_0
#define MIC_SAMPLE_RATE 16000
#define MIC_BCLK_GPIO GPIO_NUM_5
#define MIC_WS_GPIO GPIO_NUM_6
#define MIC_DATA_GPIO GPIO_NUM_4
#define MIC_USE_RIGHT_SLOT 1
#define MIC_DMA_BUF_LEN 256
#define MIC_DMA_BUF_COUNT 8

static void mic_reader_task(void *arg)
{
    (void)arg;

    int32_t *samples = calloc(MIC_DMA_BUF_LEN, sizeof(int32_t));
    if (samples == NULL) {
        ESP_LOGE(TAG, "failed to allocate sample buffer");
        vTaskDelete(NULL);
        return;
    }

    while (true) {
        size_t bytes_read = 0;
        esp_err_t err = i2s_read(
            MIC_I2S_PORT,
            samples,
            MIC_DMA_BUF_LEN * sizeof(int32_t),
            &bytes_read,
            portMAX_DELAY
        );
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "i2s_read failed: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        size_t sample_count = bytes_read / sizeof(int32_t);
        uint32_t raw_peak = 0;
        for (size_t i = 0; i < sample_count; i++) {
            uint32_t raw_abs = (uint32_t)(samples[i] < 0 ? -samples[i] : samples[i]);
            if (raw_abs > raw_peak) {
                raw_peak = raw_abs;
            }
        }

        printf(
            "raw_peak=%lu sample0=%ld sample1=%ld sample2=%ld count=%u\n",
            (unsigned long)raw_peak,
            (long)(sample_count > 0 ? samples[0] : 0),
            (long)(sample_count > 1 ? samples[1] : 0),
            (long)(sample_count > 2 ? samples[2] : 0),
            (unsigned int)sample_count
        );

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

void app_main(void)
{
    const i2s_config_t i2s_cfg = {
        .mode = I2S_MODE_MASTER | I2S_MODE_RX,
        .sample_rate = MIC_SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = MIC_USE_RIGHT_SLOT ? I2S_CHANNEL_FMT_ONLY_RIGHT : I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = MIC_DMA_BUF_COUNT,
        .dma_buf_len = MIC_DMA_BUF_LEN,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0,
    };

    const i2s_pin_config_t pin_cfg = {
        .bck_io_num = MIC_BCLK_GPIO,
        .ws_io_num = MIC_WS_GPIO,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = MIC_DATA_GPIO,
        .mck_io_num = I2S_PIN_NO_CHANGE,
    };

    ESP_ERROR_CHECK(i2s_driver_install(MIC_I2S_PORT, &i2s_cfg, 0, NULL));
    ESP_ERROR_CHECK(i2s_set_pin(MIC_I2S_PORT, &pin_cfg));
    ESP_ERROR_CHECK(i2s_zero_dma_buffer(MIC_I2S_PORT));

    ESP_LOGI(
        TAG,
        "minimal INMP441 test started on BCLK=%d WS=%d DIN=%d slot=%s",
        MIC_BCLK_GPIO,
        MIC_WS_GPIO,
        MIC_DATA_GPIO,
        MIC_USE_RIGHT_SLOT ? "RIGHT" : "LEFT"
    );

    xTaskCreate(mic_reader_task, "mic_reader", 4096, NULL, 5, NULL);
}
