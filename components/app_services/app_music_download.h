#ifndef APP_MUSIC_DOWNLOAD_H
#define APP_MUSIC_DOWNLOAD_H

#include "esp_err.h"

typedef void (*app_music_download_status_cb_t)(const char *status_text, void *ctx);

esp_err_t app_music_download_default_wav_to_sd(
    const char *destination_path,
    app_music_download_status_cb_t status_cb,
    void *status_ctx
);

#endif
