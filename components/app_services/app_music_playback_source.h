#ifndef APP_MUSIC_PLAYBACK_SOURCE_H
#define APP_MUSIC_PLAYBACK_SOURCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_music_download.h"
#include "app_music_service.h"
#include "app_music_source.h"
#include "esp_err.h"

typedef esp_err_t (*app_music_playback_source_action_cb_t)(void *ctx);

typedef struct {
    app_music_playback_source_action_cb_t ensure_sd_ready;
    app_music_playback_source_action_cb_t refresh_sd_listing;
    app_music_download_status_cb_t status_cb;
    void *ctx;
} app_music_playback_source_env_t;

typedef struct {
    app_music_source_handle_t handle;
    app_music_source_t backend_source;
    bool backend_open;
} app_music_playback_source_t;

void app_music_playback_source_reset(app_music_playback_source_t *source);
void app_music_playback_source_close(app_music_playback_source_t *source);
bool app_music_playback_source_is_open(const app_music_playback_source_t *source);
bool app_music_playback_source_is_tone(const app_music_playback_source_t *source);
bool app_music_playback_source_is_file(const app_music_playback_source_t *source);
bool app_music_playback_source_is_usb(const app_music_playback_source_t *source);
uint32_t app_music_playback_source_get_sample_rate(const app_music_playback_source_t *source);
const char *app_music_playback_source_playing_status_text(app_music_source_t source);

esp_err_t app_music_playback_source_open(
    app_music_playback_source_t *source,
    const app_music_snapshot_t *snapshot,
    const app_music_playback_source_env_t *env
);
esp_err_t app_music_playback_source_update(
    app_music_playback_source_t *source,
    const app_music_snapshot_t *snapshot
);
esp_err_t app_music_playback_source_read(
    app_music_playback_source_t *source,
    int16_t *sample_buffer,
    size_t frame_count,
    size_t *out_frame_count,
    bool *out_finished,
    const app_music_playback_source_env_t *env
);

#endif
