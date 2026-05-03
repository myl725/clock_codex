#include "app_music_playback_source.h"

#include <stdarg.h>
#include <string.h>

#include "app_config.h"
#include "app_music_usb_stream.h"
#include "esp_check.h"

typedef struct app_music_playback_backend app_music_playback_backend_t;

struct app_music_playback_backend {
    app_music_source_t source_id;
    bool is_file;
    const char *playing_status_text;
    esp_err_t (*open)(
        app_music_playback_source_t *source,
        const app_music_snapshot_t *snapshot,
        const app_music_playback_source_env_t *env
    );
    esp_err_t (*update)(
        app_music_playback_source_t *source,
        const app_music_snapshot_t *snapshot
    );
    esp_err_t (*read)(
        app_music_playback_source_t *source,
        int16_t *sample_buffer,
        size_t frame_count,
        size_t *out_frame_count,
        bool *out_finished,
        const app_music_playback_source_env_t *env
    );
};

static void app_music_playback_source_report_status(
    const app_music_playback_source_env_t *env,
    const char *status_text
)
{
    if (env == NULL || env->status_cb == NULL || status_text == NULL) {
        return;
    }

    env->status_cb(status_text, env->ctx);
}

static void app_music_playback_source_report_status_fmt(
    const app_music_playback_source_env_t *env,
    const char *format,
    ...
)
{
    char buffer[APP_MUSIC_WAV_PATH_LENGTH];
    va_list args;

    if (env == NULL || env->status_cb == NULL || format == NULL) {
        return;
    }

    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    env->status_cb(buffer, env->ctx);
}

static void app_music_playback_source_set_opening_status(
    app_music_source_kind_t kind,
    const app_music_playback_source_env_t *env
)
{
    switch (kind) {
        case APP_MUSIC_SOURCE_KIND_WAV_FILE:
            app_music_playback_source_report_status(env, "Opening WAV file");
            break;
        case APP_MUSIC_SOURCE_KIND_MP3_FILE:
            app_music_playback_source_report_status(env, "Opening MP3 file");
            break;
        default:
            app_music_playback_source_report_status(env, "Opening audio source");
            break;
    }
}

static void app_music_playback_source_set_open_success_status(
    app_music_source_kind_t kind,
    const app_music_playback_source_env_t *env
)
{
    if (kind == APP_MUSIC_SOURCE_KIND_WAV_FILE) {
        app_music_playback_source_report_status(env, "Validating WAV header");
    }
}

static void app_music_playback_source_set_open_failure_status(
    app_music_source_kind_t kind,
    esp_err_t err,
    const char *detail,
    const app_music_playback_source_env_t *env
)
{
    switch (kind) {
        case APP_MUSIC_SOURCE_KIND_MP3_FILE:
            if (detail != NULL && detail[0] != '\0') {
                app_music_playback_source_report_status_fmt(
                    env,
                    "MP3 open failed: %s (%s)",
                    esp_err_to_name(err),
                    detail
                );
            } else {
                app_music_playback_source_report_status_fmt(env, "MP3 open failed: %s", esp_err_to_name(err));
            }
            break;
        case APP_MUSIC_SOURCE_KIND_WAV_FILE:
            app_music_playback_source_report_status_fmt(env, "WAV open failed: %s", esp_err_to_name(err));
            break;
        default:
            app_music_playback_source_report_status_fmt(env, "Audio open failed: %s", esp_err_to_name(err));
            break;
    }
}

static void app_music_playback_source_set_read_failure_status(
    app_music_source_kind_t kind,
    esp_err_t err,
    const char *detail,
    const app_music_playback_source_env_t *env
)
{
    switch (kind) {
        case APP_MUSIC_SOURCE_KIND_MP3_FILE:
            if (detail != NULL && detail[0] != '\0') {
                app_music_playback_source_report_status_fmt(
                    env,
                    "MP3 read failed: %s (%s)",
                    esp_err_to_name(err),
                    detail
                );
            } else {
                app_music_playback_source_report_status_fmt(env, "MP3 read failed: %s", esp_err_to_name(err));
            }
            break;
        case APP_MUSIC_SOURCE_KIND_WAV_FILE:
            app_music_playback_source_report_status_fmt(env, "WAV read failed: %s", esp_err_to_name(err));
            break;
        default:
            app_music_playback_source_report_status_fmt(env, "Audio source failed: %s", esp_err_to_name(err));
            break;
    }
}

static float app_music_playback_source_frequency_from_tone(app_music_tone_t tone)
{
    switch (tone) {
        case APP_MUSIC_TONE_A4:
            return 440.0f;
        case APP_MUSIC_TONE_C5:
            return 523.25f;
        case APP_MUSIC_TONE_E5:
            return 659.25f;
        default:
            return 440.0f;
    }
}

static const app_music_playback_backend_t *app_music_playback_source_select_backend(app_music_source_t source);

static esp_err_t app_music_playback_source_open_tone_backend(
    app_music_playback_source_t *source,
    const app_music_snapshot_t *snapshot,
    const app_music_playback_source_env_t *env
)
{
    (void)env;

    return app_music_source_open_tone(
        &source->handle,
        app_music_playback_source_frequency_from_tone(snapshot->tone),
        snapshot->volume_percent,
        snapshot->sample_rate_hz > 0U ? snapshot->sample_rate_hz : 44100U
    );
}

static esp_err_t app_music_playback_source_update_tone_backend(
    app_music_playback_source_t *source,
    const app_music_snapshot_t *snapshot
)
{
    return app_music_source_update_tone(
        &source->handle,
        app_music_playback_source_frequency_from_tone(snapshot->tone),
        snapshot->volume_percent
    );
}

static esp_err_t app_music_playback_source_read_common_backend(
    app_music_playback_source_t *source,
    int16_t *sample_buffer,
    size_t frame_count,
    size_t *out_frame_count,
    bool *out_finished,
    const app_music_playback_source_env_t *env
)
{
    app_music_source_kind_t kind;
    esp_err_t err;

    kind = app_music_source_get_kind(&source->handle);
    err = app_music_source_read_stereo_frames(
        &source->handle,
        sample_buffer,
        frame_count,
        out_frame_count,
        out_finished
    );
    if (err != ESP_OK) {
        app_music_playback_source_set_read_failure_status(
            kind,
            err,
            app_music_source_get_last_detail(&source->handle),
            env
        );
    }
    return err;
}

static esp_err_t app_music_playback_source_open_file_backend(
    app_music_playback_source_t *source,
    const app_music_snapshot_t *snapshot,
    const app_music_playback_source_env_t *env
)
{
    app_music_source_kind_t kind;
    esp_err_t err;

    ESP_RETURN_ON_FALSE(env != NULL, ESP_ERR_INVALID_ARG, "", "environment is null");
    ESP_RETURN_ON_FALSE(env->ensure_sd_ready != NULL, ESP_ERR_INVALID_ARG, "", "ensure_sd_ready is null");

    err = env->ensure_sd_ready(env->ctx);
    if (err != ESP_OK) {
        return err;
    }

    kind = app_music_source_detect_kind_from_path(snapshot->wav_path);
    app_music_playback_source_set_opening_status(kind, env);
    err = app_music_source_open_file(&source->handle, snapshot->wav_path);
    if (err == ESP_ERR_NOT_FOUND && kind == APP_MUSIC_SOURCE_KIND_WAV_FILE) {
        app_music_playback_source_report_status(env, "WAV missing, downloading");
        err = app_music_download_default_wav_to_sd(snapshot->wav_path, env->status_cb, env->ctx);
        if (err == ESP_OK && env->refresh_sd_listing != NULL) {
            env->refresh_sd_listing(env->ctx);
            app_music_playback_source_report_status(env, "Opening downloaded WAV");
            err = app_music_source_open_file(&source->handle, snapshot->wav_path);
        }
    }

    if (err != ESP_OK) {
        app_music_playback_source_set_open_failure_status(
            kind,
            err,
            app_music_source_get_last_detail(&source->handle),
            env
        );
        return err;
    }

    app_music_playback_source_set_open_success_status(kind, env);
    return ESP_OK;
}

static esp_err_t app_music_playback_source_update_file_backend(
    app_music_playback_source_t *source,
    const app_music_snapshot_t *snapshot
)
{
    (void)source;
    (void)snapshot;
    return ESP_OK;
}

static esp_err_t app_music_playback_source_open_usb_backend(
    app_music_playback_source_t *source,
    const app_music_snapshot_t *snapshot,
    const app_music_playback_source_env_t *env
)
{
    uint32_t sample_rate_hz;

    (void)source;
    (void)env;

    sample_rate_hz = snapshot->sample_rate_hz > 0U ? snapshot->sample_rate_hz : 48000U;
    return app_music_usb_stream_start(sample_rate_hz);
}

static esp_err_t app_music_playback_source_update_usb_backend(
    app_music_playback_source_t *source,
    const app_music_snapshot_t *snapshot
)
{
    (void)source;
    (void)snapshot;
    return ESP_OK;
}

static esp_err_t app_music_playback_source_read_usb_backend(
    app_music_playback_source_t *source,
    int16_t *sample_buffer,
    size_t frame_count,
    size_t *out_frame_count,
    bool *out_finished,
    const app_music_playback_source_env_t *env
)
{
    (void)source;
    (void)env;

    if (out_finished != NULL) {
        *out_finished = false;
    }
    return app_music_usb_stream_read_stereo(sample_buffer, frame_count, out_frame_count);
}

static const app_music_playback_backend_t s_tone_backend = {
    .source_id = APP_MUSIC_SOURCE_TONE,
    .is_file = false,
    .playing_status_text = "Playing tone",
    .open = app_music_playback_source_open_tone_backend,
    .update = app_music_playback_source_update_tone_backend,
    .read = app_music_playback_source_read_common_backend,
};

static const app_music_playback_backend_t s_file_backend = {
    .source_id = APP_MUSIC_SOURCE_SD_WAV,
    .is_file = true,
    .playing_status_text = "Playing SD audio",
    .open = app_music_playback_source_open_file_backend,
    .update = app_music_playback_source_update_file_backend,
    .read = app_music_playback_source_read_common_backend,
};

static const app_music_playback_backend_t s_usb_backend = {
    .source_id = APP_MUSIC_SOURCE_USB_AUDIO,
    .is_file = false,
    .playing_status_text = "Playing USB audio",
    .open = app_music_playback_source_open_usb_backend,
    .update = app_music_playback_source_update_usb_backend,
    .read = app_music_playback_source_read_usb_backend,
};

static const app_music_playback_backend_t *app_music_playback_source_select_backend(app_music_source_t source)
{
    switch (source) {
        case APP_MUSIC_SOURCE_TONE:
            return &s_tone_backend;
        case APP_MUSIC_SOURCE_SD_WAV:
            return &s_file_backend;
        case APP_MUSIC_SOURCE_USB_AUDIO:
            return &s_usb_backend;
        default:
            return NULL;
    }
}

void app_music_playback_source_reset(app_music_playback_source_t *source)
{
    if (source == NULL) {
        return;
    }

    app_music_source_reset(&source->handle);
    source->backend_source = APP_MUSIC_SOURCE_COUNT;
    source->backend_open = false;
}

void app_music_playback_source_close(app_music_playback_source_t *source)
{
    const app_music_playback_backend_t *backend;

    if (source == NULL) {
        return;
    }

    backend = app_music_playback_source_select_backend(source->backend_source);
    if (backend != NULL && backend->source_id == APP_MUSIC_SOURCE_USB_AUDIO) {
        app_music_usb_stream_stop();
    }
    app_music_source_close(&source->handle);
    source->backend_source = APP_MUSIC_SOURCE_COUNT;
    source->backend_open = false;
}

bool app_music_playback_source_is_open(const app_music_playback_source_t *source)
{
    return source != NULL &&
           source->backend_source < APP_MUSIC_SOURCE_COUNT &&
           source->backend_open;
}

bool app_music_playback_source_is_tone(const app_music_playback_source_t *source)
{
    return source != NULL && source->backend_source == APP_MUSIC_SOURCE_TONE;
}

bool app_music_playback_source_is_file(const app_music_playback_source_t *source)
{
    const app_music_playback_backend_t *backend;

    if (source == NULL || source->backend_source >= APP_MUSIC_SOURCE_COUNT) {
        return false;
    }

    backend = app_music_playback_source_select_backend(source->backend_source);
    return backend != NULL && backend->is_file;
}

bool app_music_playback_source_is_usb(const app_music_playback_source_t *source)
{
    return source != NULL && source->backend_source == APP_MUSIC_SOURCE_USB_AUDIO;
}

uint32_t app_music_playback_source_get_sample_rate(const app_music_playback_source_t *source)
{
    if (source == NULL) {
        return 0U;
    }

    if (source->backend_source == APP_MUSIC_SOURCE_USB_AUDIO) {
        return app_music_usb_stream_get_sample_rate();
    }

    return app_music_source_get_sample_rate(&source->handle);
}

const char *app_music_playback_source_playing_status_text(app_music_source_t source)
{
    const app_music_playback_backend_t *backend = app_music_playback_source_select_backend(source);

    if (backend == NULL) {
        return "Playing audio";
    }

    return backend->playing_status_text;
}

esp_err_t app_music_playback_source_open(
    app_music_playback_source_t *source,
    const app_music_snapshot_t *snapshot,
    const app_music_playback_source_env_t *env
)
{
    const app_music_playback_backend_t *backend;
    esp_err_t err;

    ESP_RETURN_ON_FALSE(source != NULL, ESP_ERR_INVALID_ARG, "", "source is null");
    ESP_RETURN_ON_FALSE(snapshot != NULL, ESP_ERR_INVALID_ARG, "", "snapshot is null");

    backend = app_music_playback_source_select_backend(snapshot->source);
    ESP_RETURN_ON_FALSE(backend != NULL, ESP_ERR_NOT_SUPPORTED, "", "unsupported source type");

    err = backend->open(source, snapshot, env);
    if (err != ESP_OK) {
        return err;
    }

    source->backend_source = backend->source_id;
    source->backend_open = true;
    return ESP_OK;
}

esp_err_t app_music_playback_source_update(
    app_music_playback_source_t *source,
    const app_music_snapshot_t *snapshot
)
{
    const app_music_playback_backend_t *backend;

    ESP_RETURN_ON_FALSE(source != NULL, ESP_ERR_INVALID_ARG, "", "source is null");
    ESP_RETURN_ON_FALSE(snapshot != NULL, ESP_ERR_INVALID_ARG, "", "snapshot is null");

    if (!app_music_playback_source_is_open(source)) {
        return ESP_ERR_INVALID_STATE;
    }

    backend = app_music_playback_source_select_backend(source->backend_source);
    ESP_RETURN_ON_FALSE(backend != NULL, ESP_ERR_NOT_SUPPORTED, "", "unsupported source type");
    if (backend->update == NULL) {
        return ESP_OK;
    }

    return backend->update(source, snapshot);
}

esp_err_t app_music_playback_source_read(
    app_music_playback_source_t *source,
    int16_t *sample_buffer,
    size_t frame_count,
    size_t *out_frame_count,
    bool *out_finished,
    const app_music_playback_source_env_t *env
)
{
    const app_music_playback_backend_t *backend;

    ESP_RETURN_ON_FALSE(source != NULL, ESP_ERR_INVALID_ARG, "", "source is null");
    ESP_RETURN_ON_FALSE(sample_buffer != NULL, ESP_ERR_INVALID_ARG, "", "sample buffer is null");
    ESP_RETURN_ON_FALSE(frame_count > 0U, ESP_ERR_INVALID_ARG, "", "frame_count must be positive");
    ESP_RETURN_ON_FALSE(app_music_playback_source_is_open(source), ESP_ERR_INVALID_STATE, "", "source is not open");

    backend = app_music_playback_source_select_backend(source->backend_source);
    ESP_RETURN_ON_FALSE(backend != NULL && backend->read != NULL, ESP_ERR_NOT_SUPPORTED, "", "unsupported source type");
    return backend->read(source, sample_buffer, frame_count, out_frame_count, out_finished, env);
}
