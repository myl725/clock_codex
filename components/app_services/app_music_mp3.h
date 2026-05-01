#ifndef APP_MUSIC_MP3_H
#define APP_MUSIC_MP3_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "esp_err.h"

typedef struct {
    FILE *handle;
    void *decoder_state;
    uint32_t sample_rate_hz;
    uint16_t channel_count;
} app_music_mp3_file_t;

esp_err_t app_music_mp3_open(const char *path, app_music_mp3_file_t *out_file);
esp_err_t app_music_mp3_read_stereo_frames(
    app_music_mp3_file_t *mp3_file,
    int16_t *out_buffer,
    size_t max_frame_count,
    size_t *out_frame_count,
    bool *out_reached_eof
);
const char *app_music_mp3_get_last_detail(void);
void app_music_mp3_close(app_music_mp3_file_t *mp3_file);

#endif
