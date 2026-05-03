#include "display_ili9341.h"

#include <assert.h>
#include <stdbool.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "board_config.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#define ILI9341_CMD  0
#define ILI9341_DATA 1
#define ILI9341_MAX_TRANSFER_BYTES 16384U

typedef struct {
    uint8_t cmd;
    uint8_t data[16];
    uint8_t databytes;
} lcd_init_cmd_t;

static const char *TAG = "display_ili9341";

static spi_device_handle_t s_lcd_spi;
static lv_disp_draw_buf_t s_draw_buf;
static lv_color_t *s_buf_1;
static lv_color_t *s_buf_2;
static spi_transaction_t s_spi_trans;
static bool s_display_initialized;

DRAM_ATTR static const lcd_init_cmd_t s_ili9341_init_cmds[] = {
    {0xCF, {0x00, 0x83, 0x30}, 3},
    {0xED, {0x64, 0x03, 0x12, 0x81}, 4},
    {0xE8, {0x85, 0x01, 0x79}, 3},
    {0xCB, {0x39, 0x2C, 0x00, 0x34, 0x02}, 5},
    {0xF7, {0x20}, 1},
    {0xEA, {0x00, 0x00}, 2},
    {0xC0, {0x26}, 1},
    {0xC1, {0x11}, 1},
    {0xC5, {0x35, 0x3E}, 2},
    {0xC7, {0xBE}, 1},
    {0x36, {0x28}, 1},
    {0x3A, {0x55}, 1},
    {0xB1, {0x00, 0x1B}, 2},
    {0xF2, {0x08}, 1},
    {0x26, {0x01}, 1},
    {0xE0, {0x1F, 0x1A, 0x18, 0x0A, 0x0F, 0x06, 0x45, 0x87, 0x32, 0x0A, 0x07, 0x02, 0x07, 0x05, 0x00}, 15},
    {0xE1, {0x00, 0x25, 0x27, 0x05, 0x10, 0x09, 0x3A, 0x78, 0x4D, 0x05, 0x18, 0x0D, 0x38, 0x3A, 0x1F}, 15},
    {0x2A, {0x00, 0x00, 0x00, 0xEF}, 4},
    {0x2B, {0x00, 0x00, 0x01, 0x3F}, 4},
    {0x2C, {0}, 0},
    {0xB7, {0x07}, 1},
    {0xB6, {0x0A, 0x82, 0x27, 0x00}, 4},
    {0x11, {0}, 0x80},
    {0x29, {0}, 0x80},
    {0, {0}, 0xFF},
};

static void lcd_spi_pre_transfer_cb(spi_transaction_t *transaction)
{
    int dc = (int)transaction->user;
    gpio_set_level(BOARD_LCD_PIN_DC, dc);
}

static void lcd_send_command(uint8_t cmd)
{
    spi_transaction_t transaction = {0};
    transaction.length = 8;
    transaction.tx_buffer = &cmd;
    transaction.user = (void *)ILI9341_CMD;
    ESP_ERROR_CHECK(spi_device_polling_transmit(s_lcd_spi, &transaction));
}

static void lcd_send_data(const void *data, size_t len_bytes)
{
    if (len_bytes == 0) {
        return;
    }

    spi_transaction_t transaction = {0};
    transaction.length = len_bytes * 8;
    transaction.tx_buffer = data;
    transaction.user = (void *)ILI9341_DATA;
    ESP_ERROR_CHECK(spi_device_polling_transmit(s_lcd_spi, &transaction));
}

static void lcd_set_rotation(uint8_t rotation)
{
    static const uint8_t madctl_table[] = {
        0x48,
        0x28,
        0x88,
        0xE8,
    };
    rotation %= (sizeof(madctl_table) / sizeof(madctl_table[0]));

    lcd_send_command(0x36);
    lcd_send_data(&madctl_table[rotation], 1);
}

static void lcd_set_window(int16_t x1, int16_t y1, int16_t x2, int16_t y2)
{
    uint8_t col_data[] = {
        (uint8_t)((x1 >> 8) & 0xFF),
        (uint8_t)(x1 & 0xFF),
        (uint8_t)((x2 >> 8) & 0xFF),
        (uint8_t)(x2 & 0xFF),
    };
    uint8_t row_data[] = {
        (uint8_t)((y1 >> 8) & 0xFF),
        (uint8_t)(y1 & 0xFF),
        (uint8_t)((y2 >> 8) & 0xFF),
        (uint8_t)(y2 & 0xFF),
    };

    lcd_send_command(0x2A);
    lcd_send_data(col_data, sizeof(col_data));

    lcd_send_command(0x2B);
    lcd_send_data(row_data, sizeof(row_data));

    lcd_send_command(0x2C);
}

static void lcd_panel_init(void)
{
    spi_bus_config_t bus_config = {
        .mosi_io_num = BOARD_LCD_PIN_MOSI,
        .miso_io_num = BOARD_LCD_PIN_MISO,
        .sclk_io_num = BOARD_LCD_PIN_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = ILI9341_MAX_TRANSFER_BYTES + 8,
    };

    spi_device_interface_config_t device_config = {
        .clock_speed_hz = 40 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = BOARD_LCD_PIN_CS,
        .queue_size = 1,
        .pre_cb = lcd_spi_pre_transfer_cb,
    };

    gpio_config_t gpio_config_dc_rst = {
        .pin_bit_mask = (1ULL << BOARD_LCD_PIN_DC) | (1ULL << BOARD_LCD_PIN_RST),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 1,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&gpio_config_dc_rst));
    ESP_ERROR_CHECK(spi_bus_initialize(BOARD_LCD_HOST, &bus_config, SPI_DMA_CH_AUTO));
    ESP_ERROR_CHECK(spi_bus_add_device(BOARD_LCD_HOST, &device_config, &s_lcd_spi));

    gpio_set_level(BOARD_LCD_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(BOARD_LCD_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(100));

    for (int cmd = 0; s_ili9341_init_cmds[cmd].databytes != 0xFF; cmd++) {
        lcd_send_command(s_ili9341_init_cmds[cmd].cmd);
        lcd_send_data(
            s_ili9341_init_cmds[cmd].data,
            s_ili9341_init_cmds[cmd].databytes & 0x1F
        );
        if (s_ili9341_init_cmds[cmd].databytes & 0x80) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    lcd_set_rotation(1);
    ESP_LOGI(TAG, "ILI9341 panel initialized");
}

static void lvgl_flush_cb(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p)
{
    const int16_t x1 = area->x1;
    const int16_t y1 = area->y1;
    const int16_t x2 = area->x2;
    const int16_t y2 = area->y2;
    const int16_t width = x2 - x1 + 1;
    const int16_t height = y2 - y1 + 1;
    const size_t bytes_per_line = (size_t)width * sizeof(lv_color_t);
    size_t max_lines_per_chunk = ILI9341_MAX_TRANSFER_BYTES / bytes_per_line;
    size_t sent_lines = 0U;

    if (max_lines_per_chunk == 0U) {
        max_lines_per_chunk = 1U;
    }

    while (sent_lines < (size_t)height) {
        size_t remaining_lines = (size_t)height - sent_lines;
        size_t chunk_lines = remaining_lines < max_lines_per_chunk ? remaining_lines : max_lines_per_chunk;
        lv_color_t *chunk_ptr = color_p + (sent_lines * (size_t)width);
        spi_transaction_t *result = NULL;

        lcd_set_window(
            x1,
            (int16_t)(y1 + sent_lines),
            x2,
            (int16_t)(y1 + sent_lines + chunk_lines - 1U)
        );

        memset(&s_spi_trans, 0, sizeof(s_spi_trans));
        s_spi_trans.length = bytes_per_line * chunk_lines * 8U;
        s_spi_trans.tx_buffer = chunk_ptr;
        s_spi_trans.user = (void *)ILI9341_DATA;

        ESP_ERROR_CHECK(spi_device_queue_trans(s_lcd_spi, &s_spi_trans, portMAX_DELAY));
        ESP_ERROR_CHECK(spi_device_get_trans_result(s_lcd_spi, &result, portMAX_DELAY));
        sent_lines += chunk_lines;
    }

    lv_disp_flush_ready(disp_drv);
}

void display_ili9341_init(void)
{
    if (s_display_initialized) {
        ESP_LOGW(TAG, "display driver already initialized");
        return;
    }

    lcd_panel_init();

    s_buf_1 = heap_caps_malloc(
        BOARD_LCD_HOR_RES * BOARD_LCD_DRAW_BUF_LINES * sizeof(lv_color_t),
        MALLOC_CAP_DMA
    );
    s_buf_2 = heap_caps_malloc(
        BOARD_LCD_HOR_RES * BOARD_LCD_DRAW_BUF_LINES * sizeof(lv_color_t),
        MALLOC_CAP_DMA
    );

    assert(s_buf_1 != NULL);
    assert(s_buf_2 != NULL);

    lv_disp_draw_buf_init(
        &s_draw_buf,
        s_buf_1,
        s_buf_2,
        BOARD_LCD_HOR_RES * BOARD_LCD_DRAW_BUF_LINES
    );

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = BOARD_LCD_HOR_RES;
    disp_drv.ver_res = BOARD_LCD_VER_RES;
    disp_drv.flush_cb = lvgl_flush_cb;
    disp_drv.draw_buf = &s_draw_buf;
    lv_disp_drv_register(&disp_drv);
    s_display_initialized = true;

    ESP_LOGI(TAG, "LVGL display driver registered");
}
