#ifndef APP_MUSIC_SOURCE_H
#define APP_MUSIC_SOURCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_music_mp3.h"
#include "app_music_wav.h"
#include "esp_err.h"

typedef enum {
    APP_MUSIC_SOURCE_KIND_NONE = 0,
    APP_MUSIC_SOURCE_KIND_TONE,
    APP_MUSIC_SOURCE_KIND_WAV_FILE,
    APP_MUSIC_SOURCE_KIND_MP3_FILE,
} app_music_source_kind_t;

typedef struct {
    app_music_source_kind_t kind;
    uint32_t sample_rate_hz;
    char last_detail[96];
    union {
        struct {
            float phase;
            float frequency_hz;
            uint8_t volume_percent;
        } tone;
        app_music_wav_file_t wav;
        app_music_mp3_file_t mp3;
    } state;
} app_music_source_handle_t;

void app_music_source_reset(app_music_source_handle_t *handle);
bool app_music_source_is_open(const app_music_source_handle_t *handle);
bool app_music_source_is_tone(const app_music_source_handle_t *handle);
bool app_music_source_is_file(const app_music_source_handle_t *handle);
app_music_source_kind_t app_music_source_detect_kind_from_path(const char *path);
app_music_source_kind_t app_music_source_get_kind(const app_music_source_handle_t *handle);
uint32_t app_music_source_get_sample_rate(const app_music_source_handle_t *handle);
const char *app_music_source_get_last_detail(const app_music_source_handle_t *handle);

esp_err_t app_music_source_open_tone(
    app_music_source_handle_t *handle,
    float frequency_hz,
    uint8_t volume_percent,
    uint32_t sample_rate_hz
);
esp_err_t app_music_source_update_tone(
    app_music_source_handle_t *handle,
    float frequency_hz,
    uint8_t volume_percent
);
esp_err_t app_music_source_open_file(app_music_source_handle_t *handle, const char *path);
esp_err_t app_music_source_read_stereo_frames(
    app_music_source_handle_t *handle,
    int16_t *out_buffer,
    size_t max_frame_count,
    size_t *out_frame_count,
    bool *out_finished
);
void app_music_source_close(app_music_source_handle_t *handle);

#endif
