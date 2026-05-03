#ifndef APP_MUSIC_USB_STREAM_H
#define APP_MUSIC_USB_STREAM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    bool active;
    uint32_t sample_rate_hz;
    size_t buffered_frames;
    size_t capacity_frames;
    uint32_t underrun_count;
    uint32_t overflow_count;
} app_music_usb_stream_status_t;

esp_err_t app_music_usb_stream_init(void);
esp_err_t app_music_usb_stream_start(uint32_t sample_rate_hz);
void app_music_usb_stream_stop(void);
bool app_music_usb_stream_is_active(void);
uint32_t app_music_usb_stream_get_sample_rate(void);
esp_err_t app_music_usb_stream_push_stereo(
    const int16_t *samples,
    size_t frame_count,
    size_t *out_accepted_frames
);
esp_err_t app_music_usb_stream_read_stereo(
    int16_t *out_samples,
    size_t max_frame_count,
    size_t *out_frame_count
);
void app_music_usb_stream_get_status(app_music_usb_stream_status_t *out_status);

#endif
