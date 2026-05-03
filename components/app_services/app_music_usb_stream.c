#include "app_music_usb_stream.h"

#include <string.h>

#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define APP_MUSIC_USB_STREAM_CAPACITY_FRAMES 4096U

static SemaphoreHandle_t s_usb_stream_mutex;
static int16_t s_usb_stream_buffer[APP_MUSIC_USB_STREAM_CAPACITY_FRAMES * 2U];
static size_t s_usb_stream_read_index;
static size_t s_usb_stream_write_index;
static size_t s_usb_stream_buffered_frames;
static uint32_t s_usb_stream_sample_rate_hz;
static uint32_t s_usb_stream_underrun_count;
static uint32_t s_usb_stream_overflow_count;
static bool s_usb_stream_active;

static esp_err_t app_music_usb_stream_ensure_init(void)
{
    if (s_usb_stream_mutex != NULL) {
        return ESP_OK;
    }

    s_usb_stream_mutex = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_usb_stream_mutex != NULL, ESP_ERR_NO_MEM, "", "failed to create usb stream mutex");
    return ESP_OK;
}

static void app_music_usb_stream_reset_locked(void)
{
    s_usb_stream_read_index = 0U;
    s_usb_stream_write_index = 0U;
    s_usb_stream_buffered_frames = 0U;
    s_usb_stream_underrun_count = 0U;
    s_usb_stream_overflow_count = 0U;
    memset(s_usb_stream_buffer, 0, sizeof(s_usb_stream_buffer));
}

esp_err_t app_music_usb_stream_init(void)
{
    return app_music_usb_stream_ensure_init();
}

esp_err_t app_music_usb_stream_start(uint32_t sample_rate_hz)
{
    ESP_RETURN_ON_FALSE(sample_rate_hz > 0U, ESP_ERR_INVALID_ARG, "", "sample rate must be positive");
    ESP_RETURN_ON_ERROR(app_music_usb_stream_ensure_init(), "", "usb stream init failed");

    xSemaphoreTake(s_usb_stream_mutex, portMAX_DELAY);
    app_music_usb_stream_reset_locked();
    s_usb_stream_sample_rate_hz = sample_rate_hz;
    s_usb_stream_active = true;
    xSemaphoreGive(s_usb_stream_mutex);
    return ESP_OK;
}

void app_music_usb_stream_stop(void)
{
    if (s_usb_stream_mutex == NULL) {
        return;
    }

    xSemaphoreTake(s_usb_stream_mutex, portMAX_DELAY);
    app_music_usb_stream_reset_locked();
    s_usb_stream_sample_rate_hz = 0U;
    s_usb_stream_active = false;
    xSemaphoreGive(s_usb_stream_mutex);
}

bool app_music_usb_stream_is_active(void)
{
    bool active = false;

    if (s_usb_stream_mutex == NULL) {
        return false;
    }

    xSemaphoreTake(s_usb_stream_mutex, portMAX_DELAY);
    active = s_usb_stream_active;
    xSemaphoreGive(s_usb_stream_mutex);
    return active;
}

uint32_t app_music_usb_stream_get_sample_rate(void)
{
    uint32_t sample_rate_hz = 0U;

    if (s_usb_stream_mutex == NULL) {
        return 0U;
    }

    xSemaphoreTake(s_usb_stream_mutex, portMAX_DELAY);
    sample_rate_hz = s_usb_stream_sample_rate_hz;
    xSemaphoreGive(s_usb_stream_mutex);
    return sample_rate_hz;
}

esp_err_t app_music_usb_stream_push_stereo(
    const int16_t *samples,
    size_t frame_count,
    size_t *out_accepted_frames
)
{
    size_t accepted_frames = 0U;

    ESP_RETURN_ON_FALSE(samples != NULL, ESP_ERR_INVALID_ARG, "", "samples is null");
    ESP_RETURN_ON_FALSE(frame_count > 0U, ESP_ERR_INVALID_ARG, "", "frame count must be positive");
    ESP_RETURN_ON_ERROR(app_music_usb_stream_ensure_init(), "", "usb stream init failed");

    xSemaphoreTake(s_usb_stream_mutex, portMAX_DELAY);
    if (!s_usb_stream_active) {
        xSemaphoreGive(s_usb_stream_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    while (accepted_frames < frame_count && s_usb_stream_buffered_frames < APP_MUSIC_USB_STREAM_CAPACITY_FRAMES) {
        size_t write_offset = s_usb_stream_write_index * 2U;
        size_t sample_offset = accepted_frames * 2U;
        s_usb_stream_buffer[write_offset] = samples[sample_offset];
        s_usb_stream_buffer[write_offset + 1U] = samples[sample_offset + 1U];
        s_usb_stream_write_index = (s_usb_stream_write_index + 1U) % APP_MUSIC_USB_STREAM_CAPACITY_FRAMES;
        s_usb_stream_buffered_frames++;
        accepted_frames++;
    }

    if (accepted_frames < frame_count) {
        s_usb_stream_overflow_count += (uint32_t)(frame_count - accepted_frames);
    }
    xSemaphoreGive(s_usb_stream_mutex);

    if (out_accepted_frames != NULL) {
        *out_accepted_frames = accepted_frames;
    }
    return accepted_frames > 0U ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t app_music_usb_stream_read_stereo(
    int16_t *out_samples,
    size_t max_frame_count,
    size_t *out_frame_count
)
{
    size_t produced_frames = 0U;

    ESP_RETURN_ON_FALSE(out_samples != NULL, ESP_ERR_INVALID_ARG, "", "output buffer is null");
    ESP_RETURN_ON_FALSE(max_frame_count > 0U, ESP_ERR_INVALID_ARG, "", "frame count must be positive");
    ESP_RETURN_ON_ERROR(app_music_usb_stream_ensure_init(), "", "usb stream init failed");

    xSemaphoreTake(s_usb_stream_mutex, portMAX_DELAY);
    if (!s_usb_stream_active) {
        xSemaphoreGive(s_usb_stream_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    while (produced_frames < max_frame_count && s_usb_stream_buffered_frames > 0U) {
        size_t read_offset = s_usb_stream_read_index * 2U;
        size_t sample_offset = produced_frames * 2U;
        out_samples[sample_offset] = s_usb_stream_buffer[read_offset];
        out_samples[sample_offset + 1U] = s_usb_stream_buffer[read_offset + 1U];
        s_usb_stream_read_index = (s_usb_stream_read_index + 1U) % APP_MUSIC_USB_STREAM_CAPACITY_FRAMES;
        s_usb_stream_buffered_frames--;
        produced_frames++;
    }

    if (produced_frames < max_frame_count) {
        memset(
            out_samples + (produced_frames * 2U),
            0,
            (max_frame_count - produced_frames) * 2U * sizeof(int16_t)
        );
        s_usb_stream_underrun_count += (uint32_t)(max_frame_count - produced_frames);
    }
    xSemaphoreGive(s_usb_stream_mutex);

    if (out_frame_count != NULL) {
        *out_frame_count = produced_frames;
    }
    return ESP_OK;
}

void app_music_usb_stream_get_status(app_music_usb_stream_status_t *out_status)
{
    if (out_status == NULL) {
        return;
    }

    memset(out_status, 0, sizeof(*out_status));
    out_status->capacity_frames = APP_MUSIC_USB_STREAM_CAPACITY_FRAMES;

    if (s_usb_stream_mutex == NULL) {
        return;
    }

    xSemaphoreTake(s_usb_stream_mutex, portMAX_DELAY);
    out_status->active = s_usb_stream_active;
    out_status->sample_rate_hz = s_usb_stream_sample_rate_hz;
    out_status->buffered_frames = s_usb_stream_buffered_frames;
    out_status->underrun_count = s_usb_stream_underrun_count;
    out_status->overflow_count = s_usb_stream_overflow_count;
    xSemaphoreGive(s_usb_stream_mutex);
}
