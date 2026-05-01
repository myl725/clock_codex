#include "app_led_service.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "board_config.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "ws2812_driver.h"

#define APP_LED_FRAME_MS 40

typedef struct {
    const char *name;
    app_led_rgb_t rgb;
} app_led_color_entry_t;

static const char *TAG = "app_led_service";

static const char *const EFFECT_NAMES[APP_LED_EFFECT_COUNT] = {
    [APP_LED_EFFECT_OFF] = "Off",
    [APP_LED_EFFECT_SOLID] = "Solid",
    [APP_LED_EFFECT_BLINK] = "Blink",
    [APP_LED_EFFECT_BREATHE] = "Breathe",
    [APP_LED_EFFECT_RAINBOW] = "Rainbow",
};

static const app_led_color_entry_t COLOR_TABLE[APP_LED_COLOR_COUNT] = {
    [APP_LED_COLOR_SUNSET] = {"Sunset", {255, 120, 48}},
    [APP_LED_COLOR_RED] = {"Red", {255, 48, 48}},
    [APP_LED_COLOR_GREEN] = {"Green", {48, 220, 96}},
    [APP_LED_COLOR_BLUE] = {"Blue", {72, 136, 255}},
    [APP_LED_COLOR_CYAN] = {"Cyan", {72, 224, 255}},
    [APP_LED_COLOR_MAGENTA] = {"Magenta", {224, 72, 255}},
    [APP_LED_COLOR_WHITE] = {"White", {255, 255, 255}},
};

static SemaphoreHandle_t s_state_mutex;
static app_led_snapshot_t s_snapshot;
static bool s_initialized;

static uint8_t app_led_scale_channel(uint8_t channel, uint8_t level)
{
    return (uint8_t)(((uint16_t)channel * level) / 255);
}

static ws2812_rgb_t app_led_make_output_color(app_led_rgb_t rgb, uint8_t level)
{
    ws2812_rgb_t color = {
        .red = app_led_scale_channel(rgb.red, level),
        .green = app_led_scale_channel(rgb.green, level),
        .blue = app_led_scale_channel(rgb.blue, level),
    };

    return color;
}

static uint8_t app_led_triangle_level(uint16_t phase)
{
    uint16_t normalized = phase % 200;
    if (normalized > 100) {
        normalized = 200 - normalized;
    }

    return (uint8_t)((normalized * 255) / 100);
}

static ws2812_rgb_t app_led_hsv_to_rgb(uint16_t hue)
{
    uint16_t region = (hue / 60U) % 6U;
    uint16_t remainder = ((hue % 60U) * 255U) / 60U;
    uint8_t up = (uint8_t)remainder;
    uint8_t down = (uint8_t)(255U - remainder);

    switch (region) {
        case 0:
            return (ws2812_rgb_t){255, up, 0};
        case 1:
            return (ws2812_rgb_t){down, 255, 0};
        case 2:
            return (ws2812_rgb_t){0, 255, up};
        case 3:
            return (ws2812_rgb_t){0, down, 255};
        case 4:
            return (ws2812_rgb_t){up, 0, 255};
        default:
            return (ws2812_rgb_t){255, 0, down};
    }
}

static bool app_led_service_effect_is_valid(app_led_effect_t effect)
{
    return effect >= APP_LED_EFFECT_OFF && effect < APP_LED_EFFECT_COUNT;
}

static bool app_led_service_color_is_valid(app_led_color_t color)
{
    return color >= APP_LED_COLOR_SUNSET && color < APP_LED_COLOR_COUNT;
}

static void app_led_service_copy_snapshot(app_led_snapshot_t *out_snapshot)
{
    if (out_snapshot == NULL) {
        return;
    }

    if (s_state_mutex == NULL) {
        memset(out_snapshot, 0, sizeof(*out_snapshot));
        return;
    }

    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    *out_snapshot = s_snapshot;
    xSemaphoreGive(s_state_mutex);
}

static void app_led_service_task(void *arg)
{
    (void)arg;

    app_led_snapshot_t snapshot = {0};
    ws2812_rgb_t pixels[BOARD_WS2812_LED_COUNT];
    ws2812_rgb_t last_pixels[BOARD_WS2812_LED_COUNT];
    uint16_t frame_counter = 0;
    bool has_last_pixels = false;

    memset(last_pixels, 0, sizeof(last_pixels));

    while (true) {
        app_led_service_copy_snapshot(&snapshot);

        if (!snapshot.ready) {
            vTaskDelay(pdMS_TO_TICKS(APP_LED_FRAME_MS));
            continue;
        }

        app_led_rgb_t base_rgb = app_led_service_get_color_rgb(snapshot.color);

        switch (snapshot.effect) {
            case APP_LED_EFFECT_OFF:
                for (size_t i = 0; i < BOARD_WS2812_LED_COUNT; i++) {
                    pixels[i] = (ws2812_rgb_t){0, 0, 0};
                }
                break;
            case APP_LED_EFFECT_SOLID: {
                ws2812_rgb_t solid = app_led_make_output_color(base_rgb, 255);
                for (size_t i = 0; i < BOARD_WS2812_LED_COUNT; i++) {
                    pixels[i] = solid;
                }
                break;
            }
            case APP_LED_EFFECT_BLINK: {
                uint8_t level = ((frame_counter / 10U) % 2U) ? 255 : 0;
                ws2812_rgb_t blink = app_led_make_output_color(base_rgb, level);
                for (size_t i = 0; i < BOARD_WS2812_LED_COUNT; i++) {
                    pixels[i] = blink;
                }
                break;
            }
            case APP_LED_EFFECT_BREATHE: {
                uint8_t level = app_led_triangle_level(frame_counter % 200U);
                ws2812_rgb_t breathe = app_led_make_output_color(base_rgb, level);
                for (size_t i = 0; i < BOARD_WS2812_LED_COUNT; i++) {
                    pixels[i] = breathe;
                }
                break;
            }
            case APP_LED_EFFECT_RAINBOW:
                for (size_t i = 0; i < BOARD_WS2812_LED_COUNT; i++) {
                    uint16_t hue = (uint16_t)((frame_counter * 6U) + ((360U * i) / BOARD_WS2812_LED_COUNT));
                    pixels[i] = app_led_hsv_to_rgb((uint16_t)(hue % 360U));
                }
                break;
            default:
                break;
        }

        if (!has_last_pixels || memcmp(last_pixels, pixels, sizeof(pixels)) != 0) {
            if (BOARD_WS2812_LED_COUNT == 1) {
                ESP_ERROR_CHECK(ws2812_set_all(pixels[0]));
            } else {
                ESP_ERROR_CHECK(ws2812_set_pixels(pixels, BOARD_WS2812_LED_COUNT));
            }
            memcpy(last_pixels, pixels, sizeof(pixels));
            has_last_pixels = true;
        }

        frame_counter++;
        vTaskDelay(pdMS_TO_TICKS(APP_LED_FRAME_MS));
    }
}

esp_err_t app_led_service_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "service already initialized");
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(ws2812_init(), TAG, "failed to init WS2812 driver");

    s_state_mutex = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_state_mutex != NULL, ESP_ERR_NO_MEM, TAG, "failed to create state mutex");

    memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_snapshot.ready = true;
    s_snapshot.effect = APP_LED_EFFECT_SOLID;
    s_snapshot.color = APP_LED_COLOR_SUNSET;

    BaseType_t task_ok = xTaskCreatePinnedToCore(
        app_led_service_task,
        "app_led_task",
        1024 * 4,
        NULL,
        3,
        NULL,
        1
    );
    ESP_RETURN_ON_FALSE(task_ok == pdPASS, ESP_ERR_NO_MEM, TAG, "failed to create LED task");

    s_initialized = true;
    ESP_LOGI(TAG, "LED service initialized");
    return ESP_OK;
}

esp_err_t app_led_service_set_effect(app_led_effect_t effect)
{
    ESP_RETURN_ON_FALSE(app_led_service_effect_is_valid(effect), ESP_ERR_INVALID_ARG, TAG, "invalid effect");
    ESP_RETURN_ON_FALSE(s_state_mutex != NULL, ESP_ERR_INVALID_STATE, TAG, "service not initialized");

    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    s_snapshot.effect = effect;
    xSemaphoreGive(s_state_mutex);

    return ESP_OK;
}

esp_err_t app_led_service_set_color(app_led_color_t color)
{
    ESP_RETURN_ON_FALSE(app_led_service_color_is_valid(color), ESP_ERR_INVALID_ARG, TAG, "invalid color");
    ESP_RETURN_ON_FALSE(s_state_mutex != NULL, ESP_ERR_INVALID_STATE, TAG, "service not initialized");

    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    s_snapshot.color = color;
    xSemaphoreGive(s_state_mutex);

    return ESP_OK;
}

void app_led_service_get_snapshot(app_led_snapshot_t *out_snapshot)
{
    if (out_snapshot == NULL) {
        return;
    }

    memset(out_snapshot, 0, sizeof(*out_snapshot));
    app_led_service_copy_snapshot(out_snapshot);
}

const char *app_led_service_effect_to_text(app_led_effect_t effect)
{
    if (!app_led_service_effect_is_valid(effect)) {
        return "Unknown";
    }

    return EFFECT_NAMES[effect];
}

const char *app_led_service_color_to_text(app_led_color_t color)
{
    if (!app_led_service_color_is_valid(color)) {
        return "Unknown";
    }

    return COLOR_TABLE[color].name;
}

app_led_rgb_t app_led_service_get_color_rgb(app_led_color_t color)
{
    if (!app_led_service_color_is_valid(color)) {
        return (app_led_rgb_t){0, 0, 0};
    }

    return COLOR_TABLE[color].rgb;
}
