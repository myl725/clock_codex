#include "app_music_source.h"

#include <math.h>
#include <string.h>
#include <strings.h>

#include "esp_check.h"

#define APP_MUSIC_SOURCE_PI 3.14159265358979323846f

static void app_music_source_set_detail(app_music_source_handle_t *handle, const char *detail)
{
    if (handle == NULL) {
        return;
    }

    if (detail == NULL) {
        handle->last_detail[0] = '\0';
        return;
    }

    strncpy(handle->last_detail, detail, sizeof(handle->last_detail) - 1U);
    handle->last_detail[sizeof(handle->last_detail) - 1U] = '\0';
}

static bool app_music_source_path_is_wav(const char *path)
{
    const char *extension = strrchr(path, '.');

    return extension != NULL && strcasecmp(extension, ".wav") == 0;
}

static bool app_music_source_path_is_mp3(const char *path)
{
    const char *extension = strrchr(path, '.');

    return extension != NULL && strcasecmp(extension, ".mp3") == 0;
}

static void app_music_source_fill_tone_frames(
    app_music_source_handle_t *handle,
    int16_t *out_buffer,
    size_t frame_count
)
{
    float amplitude = 32767.0f * ((float)handle->state.tone.volume_percent / 100.0f);
    float phase_step = (2.0f * APP_MUSIC_SOURCE_PI * handle->state.tone.frequency_hz) / (float)handle->sample_rate_hz;

    for (size_t i = 0; i < frame_count; i++) {
        int16_t sample = (int16_t)(sinf(handle->state.tone.phase) * amplitude);
        out_buffer[i * 2] = sample;
        out_buffer[(i * 2) + 1] = sample;

        handle->state.tone.phase += phase_step;
        if (handle->state.tone.phase >= (2.0f * APP_MUSIC_SOURCE_PI)) {
            handle->state.tone.phase -= (2.0f * APP_MUSIC_SOURCE_PI);
        }
    }
}

void app_music_source_reset(app_music_source_handle_t *handle)
{
    if (handle == NULL) {
        return;
    }

    memset(handle, 0, sizeof(*handle));
}

bool app_music_source_is_open(const app_music_source_handle_t *handle)
{
    return handle != NULL && handle->kind != APP_MUSIC_SOURCE_KIND_NONE;
}

bool app_music_source_is_tone(const app_music_source_handle_t *handle)
{
    return handle != NULL && handle->kind == APP_MUSIC_SOURCE_KIND_TONE;
}

bool app_music_source_is_file(const app_music_source_handle_t *handle)
{
    return handle != NULL &&
           (handle->kind == APP_MUSIC_SOURCE_KIND_WAV_FILE || handle->kind == APP_MUSIC_SOURCE_KIND_MP3_FILE);
}

app_music_source_kind_t app_music_source_detect_kind_from_path(const char *path)
{
    if (path == NULL) {
        return APP_MUSIC_SOURCE_KIND_NONE;
    }

    if (app_music_source_path_is_wav(path)) {
        return APP_MUSIC_SOURCE_KIND_WAV_FILE;
    }
    if (app_music_source_path_is_mp3(path)) {
        return APP_MUSIC_SOURCE_KIND_MP3_FILE;
    }

    return APP_MUSIC_SOURCE_KIND_NONE;
}

app_music_source_kind_t app_music_source_get_kind(const app_music_source_handle_t *handle)
{
    if (handle == NULL) {
        return APP_MUSIC_SOURCE_KIND_NONE;
    }

    return handle->kind;
}

uint32_t app_music_source_get_sample_rate(const app_music_source_handle_t *handle)
{
    if (handle == NULL) {
        return 0U;
    }

    return handle->sample_rate_hz;
}

const char *app_music_source_get_last_detail(const app_music_source_handle_t *handle)
{
    if (handle == NULL) {
        return "";
    }

    return handle->last_detail;
}

esp_err_t app_music_source_open_tone(
    app_music_source_handle_t *handle,
    float frequency_hz,
    uint8_t volume_percent,
    uint32_t sample_rate_hz
)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, "", "source handle is null");
    ESP_RETURN_ON_FALSE(sample_rate_hz > 0U, ESP_ERR_INVALID_ARG, "", "sample rate must be positive");

    app_music_source_close(handle);
    handle->kind = APP_MUSIC_SOURCE_KIND_TONE;
    handle->sample_rate_hz = sample_rate_hz;
    handle->state.tone.phase = 0.0f;
    handle->state.tone.frequency_hz = frequency_hz;
    handle->state.tone.volume_percent = volume_percent;
    app_music_source_set_detail(handle, NULL);
    return ESP_OK;
}

esp_err_t app_music_source_update_tone(
    app_music_source_handle_t *handle,
    float frequency_hz,
    uint8_t volume_percent
)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, "", "source handle is null");
    ESP_RETURN_ON_FALSE(handle->kind == APP_MUSIC_SOURCE_KIND_TONE, ESP_ERR_INVALID_STATE, "", "tone source not open");

    handle->state.tone.frequency_hz = frequency_hz;
    handle->state.tone.volume_percent = volume_percent;
    return ESP_OK;
}

esp_err_t app_music_source_open_file(app_music_source_handle_t *handle, const char *path)
{
    esp_err_t err;

    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, "", "source handle is null");
    ESP_RETURN_ON_FALSE(path != NULL, ESP_ERR_INVALID_ARG, "", "path is null");

    app_music_source_close(handle);

    if (app_music_source_path_is_wav(path)) {
        err = app_music_wav_open(path, &handle->state.wav);
        if (err != ESP_OK) {
            return err;
        }
        handle->kind = APP_MUSIC_SOURCE_KIND_WAV_FILE;
        handle->sample_rate_hz = handle->state.wav.sample_rate_hz;
        app_music_source_set_detail(handle, NULL);
        return ESP_OK;
    }

    if (app_music_source_path_is_mp3(path)) {
        err = app_music_mp3_open(path, &handle->state.mp3);
        if (err != ESP_OK) {
            app_music_source_set_detail(handle, app_music_mp3_get_last_detail());
            return err;
        }
        handle->kind = APP_MUSIC_SOURCE_KIND_MP3_FILE;
        handle->sample_rate_hz = handle->state.mp3.sample_rate_hz;
        app_music_source_set_detail(handle, NULL);
        return ESP_OK;
    }

    app_music_source_set_detail(handle, "unsupported extension");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t app_music_source_read_stereo_frames(
    app_music_source_handle_t *handle,
    int16_t *out_buffer,
    size_t max_frame_count,
    size_t *out_frame_count,
    bool *out_finished
)
{
    size_t frame_count = 0;
    bool reached_eof = false;
    esp_err_t err;

    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, "", "source handle is null");
    ESP_RETURN_ON_FALSE(out_buffer != NULL, ESP_ERR_INVALID_ARG, "", "output buffer is null");
    ESP_RETURN_ON_FALSE(max_frame_count > 0U, ESP_ERR_INVALID_ARG, "", "frame count must be positive");
    ESP_RETURN_ON_FALSE(app_music_source_is_open(handle), ESP_ERR_INVALID_STATE, "", "source is not open");

    if (out_frame_count != NULL) {
        *out_frame_count = 0U;
    }
    if (out_finished != NULL) {
        *out_finished = false;
    }

    switch (handle->kind) {
        case APP_MUSIC_SOURCE_KIND_TONE:
            app_music_source_fill_tone_frames(handle, out_buffer, max_frame_count);
            frame_count = max_frame_count;
            break;
        case APP_MUSIC_SOURCE_KIND_WAV_FILE:
            err = app_music_wav_read_stereo_frames(
                &handle->state.wav,
                out_buffer,
                max_frame_count,
                &frame_count,
                &reached_eof
            );
            if (err != ESP_OK) {
                return err;
            }
            break;
        case APP_MUSIC_SOURCE_KIND_MP3_FILE:
            err = app_music_mp3_read_stereo_frames(
                &handle->state.mp3,
                out_buffer,
                max_frame_count,
                &frame_count,
                &reached_eof
            );
            if (err != ESP_OK) {
                app_music_source_set_detail(handle, app_music_mp3_get_last_detail());
                return err;
            }
            break;
        default:
            return ESP_ERR_INVALID_STATE;
    }

    if (frame_count < max_frame_count) {
        memset(
            out_buffer + (frame_count * 2U),
            0,
            (max_frame_count - frame_count) * 2U * sizeof(int16_t)
        );
    }

    if (out_frame_count != NULL) {
        *out_frame_count = frame_count;
    }
    if (out_finished != NULL) {
        *out_finished = reached_eof;
    }
    return ESP_OK;
}

void app_music_source_close(app_music_source_handle_t *handle)
{
    if (handle == NULL) {
        return;
    }

    switch (handle->kind) {
        case APP_MUSIC_SOURCE_KIND_WAV_FILE:
            app_music_wav_close(&handle->state.wav);
            break;
        case APP_MUSIC_SOURCE_KIND_MP3_FILE:
            app_music_mp3_close(&handle->state.mp3);
            break;
        default:
            break;
    }

    app_music_source_reset(handle);
}
