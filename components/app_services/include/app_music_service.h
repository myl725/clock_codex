#ifndef APP_MUSIC_SERVICE_H
#define APP_MUSIC_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define APP_MUSIC_WAV_PATH_LENGTH 96
#define APP_MUSIC_WAV_OPTION_TEXT_LENGTH 384

typedef enum {
    APP_MUSIC_STATE_STOPPED = 0,
    APP_MUSIC_STATE_PLAYING,
} app_music_state_t;

typedef enum {
    APP_MUSIC_SOURCE_TONE = 0,
    APP_MUSIC_SOURCE_SD_WAV,
    APP_MUSIC_SOURCE_COUNT,
} app_music_source_t;

typedef enum {
    APP_MUSIC_TONE_A4 = 0,
    APP_MUSIC_TONE_C5,
    APP_MUSIC_TONE_E5,
    APP_MUSIC_TONE_COUNT,
} app_music_tone_t;

typedef struct {
    bool ready;
    app_music_state_t state;
    app_music_source_t source;
    app_music_tone_t tone;
    uint8_t volume_percent;
    uint32_t sample_rate_hz;
    char wav_path[APP_MUSIC_WAV_PATH_LENGTH];
    char status_text[APP_MUSIC_WAV_PATH_LENGTH];
} app_music_snapshot_t;

typedef struct {
    uint32_t boot_count;
    uint32_t mark_count;
    uint32_t successful_writes;
    esp_err_t current_error;
    esp_err_t last_failure_error;
    uint8_t current_stage;
    uint8_t last_failure_stage;
} app_music_diag_info_t;

esp_err_t app_music_service_init(void);
esp_err_t app_music_service_play(void);
esp_err_t app_music_service_stop(void);
esp_err_t app_music_service_toggle_playback(void);
esp_err_t app_music_service_set_source(app_music_source_t source);
esp_err_t app_music_service_set_tone(app_music_tone_t tone);
esp_err_t app_music_service_set_wav_path(const char *path);
esp_err_t app_music_service_refresh_wav_files(void);
esp_err_t app_music_service_select_wav_file(uint8_t index);
void app_music_service_get_wav_dropdown_options(
    char *buffer,
    size_t buffer_size,
    uint8_t *out_selected_index,
    uint8_t *out_count
);
void app_music_service_get_snapshot(app_music_snapshot_t *out_snapshot);
void app_music_service_get_diagnostics(app_music_diag_info_t *out_diag);
void app_music_service_log_boot_diagnostics(void);
const char *app_music_service_state_to_text(app_music_state_t state);
const char *app_music_service_source_to_text(app_music_source_t source);
const char *app_music_service_tone_to_text(app_music_tone_t tone);
const char *app_music_service_diag_stage_to_text(uint8_t stage);

#endif
