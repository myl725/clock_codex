#include "app_music_wav.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "app_music_wav";

typedef struct {
    char chunk_id[4];
    uint32_t chunk_size;
    char format[4];
} app_music_wav_riff_header_t;

typedef struct {
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
} app_music_wav_fmt_t;

static bool app_music_wav_id_matches(const char *actual, const char *expected)
{
    return memcmp(actual, expected, 4) == 0;
}

static esp_err_t app_music_wav_read_exact(FILE *handle, void *buffer, size_t byte_count)
{
    size_t bytes_read = fread(buffer, 1, byte_count, handle);
    return bytes_read == byte_count ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

esp_err_t app_music_wav_open(const char *path, app_music_wav_file_t *out_file)
{
    app_music_wav_riff_header_t riff_header;
    app_music_wav_fmt_t fmt = {0};
    bool fmt_found = false;
    bool data_found = false;
    uint32_t data_size_bytes = 0;
    FILE *handle;

    ESP_RETURN_ON_FALSE(path != NULL, ESP_ERR_INVALID_ARG, TAG, "path is null");
    ESP_RETURN_ON_FALSE(out_file != NULL, ESP_ERR_INVALID_ARG, TAG, "out_file is null");

    memset(out_file, 0, sizeof(*out_file));
    handle = fopen(path, "rb");
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_NOT_FOUND, TAG, "failed to open %s", path);

    if (app_music_wav_read_exact(handle, &riff_header, sizeof(riff_header)) != ESP_OK ||
        !app_music_wav_id_matches(riff_header.chunk_id, "RIFF") ||
        !app_music_wav_id_matches(riff_header.format, "WAVE")) {
        fclose(handle);
        return ESP_ERR_INVALID_RESPONSE;
    }

    while (!fmt_found || !data_found) {
        char chunk_id[4];
        uint32_t chunk_size = 0;

        if (app_music_wav_read_exact(handle, chunk_id, sizeof(chunk_id)) != ESP_OK ||
            app_music_wav_read_exact(handle, &chunk_size, sizeof(chunk_size)) != ESP_OK) {
            fclose(handle);
            return ESP_ERR_INVALID_RESPONSE;
        }

        if (app_music_wav_id_matches(chunk_id, "fmt ")) {
            if (chunk_size < sizeof(fmt)) {
                fclose(handle);
                return ESP_ERR_INVALID_SIZE;
            }
            if (app_music_wav_read_exact(handle, &fmt, sizeof(fmt)) != ESP_OK) {
                fclose(handle);
                return ESP_ERR_INVALID_RESPONSE;
            }
            if (chunk_size > sizeof(fmt)) {
                fseek(handle, (long)(chunk_size - sizeof(fmt)), SEEK_CUR);
            }
            fmt_found = true;
        } else if (app_music_wav_id_matches(chunk_id, "data")) {
            data_size_bytes = chunk_size;
            data_found = true;
            break;
        } else {
            fseek(handle, (long)chunk_size, SEEK_CUR);
        }

        if ((chunk_size & 1U) != 0U) {
            fseek(handle, 1L, SEEK_CUR);
        }
    }

    if (!fmt_found || !data_found) {
        fclose(handle);
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (fmt.audio_format != 1U) {
        fclose(handle);
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (fmt.bits_per_sample != 16U) {
        fclose(handle);
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (fmt.num_channels != 1U && fmt.num_channels != 2U) {
        fclose(handle);
        return ESP_ERR_NOT_SUPPORTED;
    }

    out_file->handle = handle;
    out_file->sample_rate_hz = fmt.sample_rate;
    out_file->channel_count = fmt.num_channels;
    out_file->bits_per_sample = fmt.bits_per_sample;
    out_file->data_size_bytes = data_size_bytes;
    out_file->data_bytes_read = 0;

    ESP_LOGI(
        TAG,
        "opened wav %s: %lu Hz, %u ch, %u bits, data=%lu bytes",
        path,
        (unsigned long)out_file->sample_rate_hz,
        (unsigned)out_file->channel_count,
        (unsigned)out_file->bits_per_sample,
        (unsigned long)out_file->data_size_bytes
    );
    return ESP_OK;
}

esp_err_t app_music_wav_read_stereo_frames(
    app_music_wav_file_t *wav_file,
    int16_t *out_buffer,
    size_t max_frame_count,
    size_t *out_frame_count,
    bool *out_reached_eof
)
{
    size_t frame_count = 0;
    size_t bytes_per_input_frame;
    size_t max_input_bytes;
    size_t bytes_read;

    ESP_RETURN_ON_FALSE(wav_file != NULL, ESP_ERR_INVALID_ARG, TAG, "wav_file is null");
    ESP_RETURN_ON_FALSE(wav_file->handle != NULL, ESP_ERR_INVALID_STATE, TAG, "wav file not open");
    ESP_RETURN_ON_FALSE(out_buffer != NULL, ESP_ERR_INVALID_ARG, TAG, "out_buffer is null");

    if (out_frame_count != NULL) {
        *out_frame_count = 0;
    }
    if (out_reached_eof != NULL) {
        *out_reached_eof = false;
    }

    bytes_per_input_frame = wav_file->channel_count * sizeof(int16_t);
    max_input_bytes = max_frame_count * bytes_per_input_frame;
    if (wav_file->data_bytes_read + max_input_bytes > wav_file->data_size_bytes) {
        max_input_bytes = wav_file->data_size_bytes - wav_file->data_bytes_read;
    }

    if (max_input_bytes == 0U) {
        if (out_reached_eof != NULL) {
            *out_reached_eof = true;
        }
        return ESP_OK;
    }

    if (wav_file->channel_count == 2U) {
        bytes_read = fread(out_buffer, 1, max_input_bytes, wav_file->handle);
        frame_count = bytes_read / bytes_per_input_frame;
    } else {
        for (frame_count = 0; frame_count < max_frame_count; frame_count++) {
            int16_t mono_sample = 0;
            if (fread(&mono_sample, sizeof(mono_sample), 1, wav_file->handle) != 1) {
                break;
            }
            out_buffer[frame_count * 2] = mono_sample;
            out_buffer[(frame_count * 2) + 1] = mono_sample;
        }
        bytes_read = frame_count * bytes_per_input_frame;
    }

    wav_file->data_bytes_read += (uint32_t)bytes_read;
    if (out_frame_count != NULL) {
        *out_frame_count = frame_count;
    }
    if (out_reached_eof != NULL) {
        *out_reached_eof = wav_file->data_bytes_read >= wav_file->data_size_bytes;
    }
    return ESP_OK;
}

void app_music_wav_close(app_music_wav_file_t *wav_file)
{
    if (wav_file == NULL) {
        return;
    }
    if (wav_file->handle != NULL) {
        fclose(wav_file->handle);
    }
    memset(wav_file, 0, sizeof(*wav_file));
}
