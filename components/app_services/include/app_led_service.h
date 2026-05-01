#ifndef APP_LED_SERVICE_H
#define APP_LED_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
    APP_LED_EFFECT_OFF = 0,
    APP_LED_EFFECT_SOLID,
    APP_LED_EFFECT_BLINK,
    APP_LED_EFFECT_BREATHE,
    APP_LED_EFFECT_RAINBOW,
    APP_LED_EFFECT_COUNT,
} app_led_effect_t;

typedef enum {
    APP_LED_COLOR_SUNSET = 0,
    APP_LED_COLOR_RED,
    APP_LED_COLOR_GREEN,
    APP_LED_COLOR_BLUE,
    APP_LED_COLOR_CYAN,
    APP_LED_COLOR_MAGENTA,
    APP_LED_COLOR_WHITE,
    APP_LED_COLOR_COUNT,
} app_led_color_t;

typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} app_led_rgb_t;

typedef struct {
    bool ready;
    app_led_effect_t effect;
    app_led_color_t color;
} app_led_snapshot_t;

esp_err_t app_led_service_init(void);
esp_err_t app_led_service_set_effect(app_led_effect_t effect);
esp_err_t app_led_service_set_color(app_led_color_t color);
void app_led_service_get_snapshot(app_led_snapshot_t *out_snapshot);
const char *app_led_service_effect_to_text(app_led_effect_t effect);
const char *app_led_service_color_to_text(app_led_color_t color);
app_led_rgb_t app_led_service_get_color_rgb(app_led_color_t color);

#endif
