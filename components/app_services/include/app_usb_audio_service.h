#ifndef APP_USB_AUDIO_SERVICE_H
#define APP_USB_AUDIO_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    bool ready;
    bool driver_installed;
    bool mounted;
    bool suspended;
    bool stream_active;
    bool master_mute;
    uint32_t sample_rate_hz;
    uint8_t stream_alt_setting;
    uint32_t packet_count;
    uint32_t accepted_frames;
    uint32_t dropped_frames;
    uint32_t read_error_count;
    uint32_t stream_begin_count;
    uint32_t stream_end_count;
    uint32_t last_packet_frames;
    int16_t master_volume_db_256;
} app_usb_audio_status_t;

esp_err_t app_usb_audio_service_init(void);
esp_err_t app_usb_audio_service_begin_stream(uint32_t sample_rate_hz);
esp_err_t app_usb_audio_service_ingest_stereo(
    const int16_t *samples,
    size_t frame_count,
    size_t *out_accepted_frames
);
void app_usb_audio_service_end_stream(void);
void app_usb_audio_service_get_status(app_usb_audio_status_t *out_status);
void app_usb_audio_service_adjust_master_volume(int16_t delta_db_256);
void app_usb_audio_service_toggle_master_mute(void);

#endif
