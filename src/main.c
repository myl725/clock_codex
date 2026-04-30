#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "app_speech_service.h"
#include "app_ui.h"
#include "display_ili9341.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nvs_flash.h"
#include "touch_xpt2046.h"

static const char *TAG = "app_main";

static void lvgl_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(1);
}

static void lvgl_task(void *arg)
{
    (void)arg;

    while (true) {
        uint32_t wait_ms = lv_timer_handler();
        if (wait_ms > 20) {
            wait_ms = 20;
        }
        vTaskDelay(pdMS_TO_TICKS(wait_ms > 0 ? wait_ms : 1));
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    lv_init();
    display_ili9341_init();
    touch_xpt2046_init();
    ESP_ERROR_CHECK(app_speech_service_init());
    app_ui_init();

    const esp_timer_create_args_t tick_timer_args = {
        .callback = lvgl_tick_cb,
        .name = "lvgl_tick",
    };
    esp_timer_handle_t tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&tick_timer_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, 1000));

    xTaskCreatePinnedToCore(lvgl_task, "lvgl_task", 1024 * 8, NULL, 2, NULL, 1);

    ESP_LOGI(TAG, "LVGL, touch, and INMP441 speech demo initialized.");
}
