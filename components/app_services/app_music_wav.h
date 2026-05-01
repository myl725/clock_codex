#ifndef APP_MUSIC_WAV_H
#define APP_MUSIC_WAV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "esp_err.h"

typedef struct {
    FILE *handle;
    uint32_t sample_rate_hz;
    uint16_t channel_count;
    uint16_t bits_per_sample;
    uint32_t data_size_bytes;
    uint32_t data_bytes_read;
} app_music_wav_file_t;

esp_err_t app_music_wav_open(const char *path, app_music_wav_file_t *out_file);
esp_err_t app_music_wav_read_stereo_frames(
    app_music_wav_file_t *wav_file,
    int16_t *out_buffer,
    size_t max_frame_count,
    size_t *out_frame_count,
    bool *out_reached_eof
);
void app_music_wav_close(app_music_wav_file_t *wav_file);

#endif
