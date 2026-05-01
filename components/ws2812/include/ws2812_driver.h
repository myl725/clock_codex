#ifndef WS2812_DRIVER_H
#define WS2812_DRIVER_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} ws2812_rgb_t;

esp_err_t ws2812_init(void);
size_t ws2812_get_led_count(void);
esp_err_t ws2812_set_all(ws2812_rgb_t color);
esp_err_t ws2812_set_pixels(const ws2812_rgb_t *pixels, size_t count);
esp_err_t ws2812_clear(void);

#endif
