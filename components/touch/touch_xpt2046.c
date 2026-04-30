#include "touch_xpt2046.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "board_config.h"
#include "esp_err.h"
#include "esp_log.h"
#include "lvgl.h"

typedef struct {
    int32_t x_offset;
    int32_t y_offset;
    float x_scale;
    float y_scale;
    bool calibrated;
} touch_calibration_t;

static const char *TAG = "touch_xpt2046";
static const uint8_t TOUCH_SAMPLE_COUNT = 3;

static spi_device_handle_t s_touch_spi;
static lv_indev_drv_t s_indev_drv;
static lv_point_t s_last_point;
static bool s_last_point_valid;
static bool s_touch_initialized;
static touch_calibration_t s_calibration = {
    .x_offset = BOARD_TOUCH_CAL_X_OFFSET,
    .y_offset = BOARD_TOUCH_CAL_Y_OFFSET,
    .x_scale = BOARD_TOUCH_CAL_X_SCALE,
    .y_scale = BOARD_TOUCH_CAL_Y_SCALE,
    .calibrated = true,
};

static uint16_t touch_median3(uint16_t a, uint16_t b, uint16_t c)
{
    if (a > b) {
        uint16_t temp = a;
        a = b;
        b = temp;
    }
    if (b > c) {
        uint16_t temp = b;
        b = c;
        c = temp;
    }
    if (a > b) {
        uint16_t temp = a;
        a = b;
        b = temp;
    }

    return b;
}

static bool touch_convert_coord(
    uint16_t raw_value,
    int32_t offset,
    float scale,
    int32_t screen_offset,
    int32_t screen_limit,
    uint16_t *out
)
{
    int32_t value = (int32_t)(((raw_value - offset) * scale) + screen_offset);
    if (value < 0) {
        value = 0;
    } else if (value >= screen_limit) {
        value = screen_limit - 1;
    }

    *out = (uint16_t)value;
    return true;
}

static uint16_t touch_read_raw(uint8_t command)
{
    spi_transaction_t transaction;
    uint8_t tx_data[3] = {command, 0x00, 0x00};
    uint8_t rx_data[3] = {0};

    memset(&transaction, 0, sizeof(transaction));
    transaction.length = 8 + 16;
    transaction.tx_buffer = tx_data;
    transaction.rx_buffer = rx_data;

    ESP_ERROR_CHECK(spi_device_polling_transmit(s_touch_spi, &transaction));
    return (uint16_t)(((rx_data[1] << 8) | rx_data[2]) >> 4);
}

static bool touch_read_raw_point(uint16_t *x, uint16_t *y, uint16_t touch_limit)
{
    uint16_t raw_x[TOUCH_SAMPLE_COUNT];
    uint16_t raw_y[TOUCH_SAMPLE_COUNT];

    for (uint8_t sample = 0; sample < TOUCH_SAMPLE_COUNT; sample++) {
        uint16_t z1 = touch_read_raw(0xB0);
        if (z1 < touch_limit) {
            return false;
        }

        raw_x[sample] = touch_read_raw(0x90);
        raw_y[sample] = touch_read_raw(0xD0);
    }

    *x = touch_median3(raw_x[0], raw_x[1], raw_x[2]);
    *y = touch_median3(raw_y[0], raw_y[1], raw_y[2]);
    return true;
}

static bool touch_get_calibrated_point(uint16_t *x, uint16_t *y)
{
    uint16_t raw_x;
    uint16_t raw_y;

    if (!s_calibration.calibrated ||
        !touch_read_raw_point(&raw_x, &raw_y, BOARD_TOUCH_THRESHOLD)) {
        return false;
    }

    if (!touch_convert_coord(
            raw_x,
            s_calibration.x_offset,
            s_calibration.x_scale,
            BOARD_TOUCH_CAL_SCREEN_X_OFFSET,
            BOARD_LCD_HOR_RES,
            x
        )) {
        return false;
    }

    if (!touch_convert_coord(
            raw_y,
            s_calibration.y_offset,
            s_calibration.y_scale,
            BOARD_TOUCH_CAL_SCREEN_Y_OFFSET,
            BOARD_LCD_VER_RES,
            y
        )) {
        return false;
    }

    return true;
}

static void touch_read_cb(lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
{
    (void)indev_drv;

    uint16_t touch_x;
    uint16_t touch_y;

    if (!touch_get_calibrated_point(&touch_x, &touch_y)) {
        data->state = LV_INDEV_STATE_REL;
        if (s_last_point_valid) {
            data->point = s_last_point;
        }
        return;
    }

    data->state = LV_INDEV_STATE_PR;
    data->point.x = touch_x;
    data->point.y = touch_y;
    s_last_point.x = touch_x;
    s_last_point.y = touch_y;
    s_last_point_valid = true;
}

void touch_xpt2046_init(void)
{
    if (s_touch_initialized) {
        ESP_LOGW(TAG, "touch driver already initialized");
        return;
    }

    spi_device_interface_config_t touch_device_config = {
        .clock_speed_hz = BOARD_TOUCH_SPI_CLOCK_HZ,
        .mode = 0,
        .spics_io_num = BOARD_TOUCH_PIN_CS,
        .queue_size = 1,
    };

    ESP_ERROR_CHECK(
        spi_bus_add_device(BOARD_LCD_HOST, &touch_device_config, &s_touch_spi)
    );

    lv_indev_drv_init(&s_indev_drv);
    s_indev_drv.type = LV_INDEV_TYPE_POINTER;
    s_indev_drv.read_cb = touch_read_cb;
    lv_indev_drv_register(&s_indev_drv);
    s_touch_initialized = true;

    ESP_LOGI(TAG, "XPT2046 touch driver registered");
}
