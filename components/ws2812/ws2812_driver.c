#include "ws2812_driver.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "board_config.h"
#include "driver/rmt_tx.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#define WS2812_RMT_RESOLUTION_HZ (10 * 1000 * 1000)

static const char *TAG = "ws2812_driver";

static rmt_channel_handle_t s_led_channel;
static rmt_encoder_handle_t s_encoder;
static uint8_t *s_pixel_buffer;
static bool s_initialized;

static const rmt_symbol_word_t WS2812_ZERO = {
    .level0 = 1,
    .duration0 = 3,
    .level1 = 0,
    .duration1 = 9,
};

static const rmt_symbol_word_t WS2812_ONE = {
    .level0 = 1,
    .duration0 = 9,
    .level1 = 0,
    .duration1 = 3,
};

static const rmt_symbol_word_t WS2812_RESET = {
    .level0 = 0,
    .duration0 = 250,
    .level1 = 0,
    .duration1 = 250,
};

static size_t ws2812_encode_bytes(
    const void *data,
    size_t data_size,
    size_t symbols_written,
    size_t symbols_free,
    rmt_symbol_word_t *symbols,
    bool *done,
    void *arg
)
{
    (void)arg;

    if (symbols_free < 8) {
        return 0;
    }

    size_t data_pos = symbols_written / 8;
    const uint8_t *bytes = (const uint8_t *)data;

    if (data_pos < data_size) {
        size_t symbol_pos = 0;
        for (uint8_t bit_mask = 0x80; bit_mask != 0; bit_mask >>= 1) {
            symbols[symbol_pos++] = (bytes[data_pos] & bit_mask) ? WS2812_ONE : WS2812_ZERO;
        }
        return symbol_pos;
    }

    symbols[0] = WS2812_RESET;
    *done = true;
    return 1;
}

static esp_err_t ws2812_refresh_locked(void)
{
    rmt_transmit_config_t tx_config = {
        .loop_count = 0,
    };

    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "driver not initialized");
    ESP_RETURN_ON_FALSE(s_pixel_buffer != NULL, ESP_ERR_INVALID_STATE, TAG, "pixel buffer missing");

    ESP_RETURN_ON_ERROR(
        rmt_transmit(
            s_led_channel,
            s_encoder,
            s_pixel_buffer,
            BOARD_WS2812_LED_COUNT * 3,
            &tx_config
        ),
        TAG,
        "failed to transmit pixel data"
    );
    ESP_RETURN_ON_ERROR(rmt_tx_wait_all_done(s_led_channel, portMAX_DELAY), TAG, "failed to flush pixel data");

    return ESP_OK;
}

esp_err_t ws2812_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "driver already initialized");
        return ESP_OK;
    }

    s_pixel_buffer = calloc(BOARD_WS2812_LED_COUNT * 3, sizeof(uint8_t));
    ESP_RETURN_ON_FALSE(s_pixel_buffer != NULL, ESP_ERR_NO_MEM, TAG, "failed to allocate pixel buffer");

    rmt_tx_channel_config_t tx_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = BOARD_WS2812_DATA_GPIO,
        .mem_block_symbols = 64,
        .resolution_hz = WS2812_RMT_RESOLUTION_HZ,
        .trans_queue_depth = 4,
    };
    ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&tx_config, &s_led_channel), TAG, "failed to create RMT TX channel");

    const rmt_simple_encoder_config_t encoder_config = {
        .callback = ws2812_encode_bytes,
    };
    ESP_RETURN_ON_ERROR(rmt_new_simple_encoder(&encoder_config, &s_encoder), TAG, "failed to create encoder");
    ESP_RETURN_ON_ERROR(rmt_enable(s_led_channel), TAG, "failed to enable RMT TX channel");

    s_initialized = true;
    ESP_RETURN_ON_ERROR(ws2812_clear(), TAG, "failed to clear strip after init");

    ESP_LOGI(
        TAG,
        "WS2812 driver ready on GPIO=%d leds=%d",
        BOARD_WS2812_DATA_GPIO,
        BOARD_WS2812_LED_COUNT
    );
    return ESP_OK;
}

size_t ws2812_get_led_count(void)
{
    return BOARD_WS2812_LED_COUNT;
}

esp_err_t ws2812_set_all(ws2812_rgb_t color)
{
    ws2812_rgb_t pixels[BOARD_WS2812_LED_COUNT];

    for (size_t i = 0; i < BOARD_WS2812_LED_COUNT; i++) {
        pixels[i] = color;
    }

    return ws2812_set_pixels(pixels, BOARD_WS2812_LED_COUNT);
}

esp_err_t ws2812_set_pixels(const ws2812_rgb_t *pixels, size_t count)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "driver not initialized");
    ESP_RETURN_ON_FALSE(pixels != NULL, ESP_ERR_INVALID_ARG, TAG, "pixels is null");
    ESP_RETURN_ON_FALSE(count == BOARD_WS2812_LED_COUNT, ESP_ERR_INVALID_ARG, TAG, "pixel count mismatch");

    for (size_t i = 0; i < count; i++) {
        s_pixel_buffer[(i * 3) + 0] = pixels[i].green;
        s_pixel_buffer[(i * 3) + 1] = pixels[i].red;
        s_pixel_buffer[(i * 3) + 2] = pixels[i].blue;
    }

    return ws2812_refresh_locked();
}

esp_err_t ws2812_clear(void)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "driver not initialized");

    memset(s_pixel_buffer, 0, BOARD_WS2812_LED_COUNT * 3);
    return ws2812_refresh_locked();
}
