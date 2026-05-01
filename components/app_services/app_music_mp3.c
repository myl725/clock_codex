#include "app_music_mp3.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_IO_SIZE (32 * 1024)
#define MINIMP3_BUF_SIZE (16 * 1024)
#include "third_party/minimp3/minimp3_ex.h"

typedef struct {
    mp3dec_ex_t decoder;
    mp3dec_io_t io;
    mp3d_sample_t primed_samples[MINIMP3_MAX_SAMPLES_PER_FRAME];
    size_t primed_sample_count;
    size_t primed_sample_offset;
    int last_channels;
    int last_hz;
} app_music_mp3_decoder_t;

static const char *TAG = "app_music_mp3";
static char s_last_detail[96];

static void app_music_mp3_set_last_detail(const char *format, ...)
{
    va_list args;

    if (format == NULL) {
        s_last_detail[0] = '\0';
        return;
    }

    va_start(args, format);
    vsnprintf(s_last_detail, sizeof(s_last_detail), format, args);
    va_end(args);
}

static const char *app_music_mp3_code_to_text(int error_code)
{
    switch (error_code) {
        case 0:
            return "ok";
        case MP3D_E_PARAM:
            return "param";
        case MP3D_E_MEMORY:
            return "memory";
        case MP3D_E_IOERROR:
            return "io";
        case MP3D_E_USER:
            return "user";
        case MP3D_E_DECODE:
            return "decode";
        default:
            return "unknown";
    }
}

const char *app_music_mp3_get_last_detail(void)
{
    return s_last_detail[0] == '\0' ? "none" : s_last_detail;
}

static size_t app_music_mp3_read_cb(void *buf, size_t size, void *user_data)
{
    FILE *handle = (FILE *)user_data;

    if (handle == NULL || buf == NULL || size == 0U) {
        return 0;
    }

    return fread(buf, 1, size, handle);
}

static int app_music_mp3_seek_cb(uint64_t position, void *user_data)
{
    FILE *handle = (FILE *)user_data;

    if (handle == NULL) {
        return -1;
    }

    return fseek(handle, (long)position, SEEK_SET);
}

static esp_err_t app_music_mp3_error_from_code(int error_code)
{
    switch (error_code) {
        case 0:
            return ESP_OK;
        case MP3D_E_PARAM:
            return ESP_ERR_INVALID_ARG;
        case MP3D_E_MEMORY:
            return ESP_ERR_NO_MEM;
        case MP3D_E_IOERROR:
            return ESP_FAIL;
        case MP3D_E_USER:
            return ESP_ERR_INVALID_RESPONSE;
        case MP3D_E_DECODE:
            return ESP_ERR_NOT_SUPPORTED;
        default:
            return ESP_FAIL;
    }
}

static esp_err_t app_music_mp3_prime_first_frame(
    app_music_mp3_decoder_t *decoder_state,
    app_music_mp3_file_t *out_file
)
{
    mp3d_sample_t *frame_buffer = NULL;
    mp3dec_frame_info_t frame_info = {0};
    size_t decoded_samples = mp3dec_ex_read_frame(
        &decoder_state->decoder,
        &frame_buffer,
        &frame_info,
        MINIMP3_MAX_SAMPLES_PER_FRAME
    );

    if (decoded_samples == 0U) {
        if (decoder_state->decoder.last_error != 0) {
            app_music_mp3_set_last_detail(
                "prime code=%d (%s)",
                decoder_state->decoder.last_error,
                app_music_mp3_code_to_text(decoder_state->decoder.last_error)
            );
            return app_music_mp3_error_from_code(decoder_state->decoder.last_error);
        }
        app_music_mp3_set_last_detail("prime returned no audio");
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (frame_info.hz <= 0 || (frame_info.channels != 1 && frame_info.channels != 2)) {
        app_music_mp3_set_last_detail("prime invalid hz=%d ch=%d", frame_info.hz, frame_info.channels);
        return ESP_ERR_NOT_SUPPORTED;
    }

    memcpy(decoder_state->primed_samples, frame_buffer, decoded_samples * sizeof(mp3d_sample_t));
    decoder_state->primed_sample_count = decoded_samples;
    decoder_state->primed_sample_offset = 0U;
    decoder_state->last_hz = frame_info.hz;
    decoder_state->last_channels = frame_info.channels;
    out_file->sample_rate_hz = (uint32_t)frame_info.hz;
    out_file->channel_count = (uint16_t)frame_info.channels;
    app_music_mp3_set_last_detail(
        "prime ok hz=%lu ch=%u samples=%u",
        (unsigned long)out_file->sample_rate_hz,
        (unsigned)out_file->channel_count,
        (unsigned)decoded_samples
    );
    return ESP_OK;
}

static esp_err_t app_music_mp3_fill_cached_frame(
    app_music_mp3_decoder_t *decoder_state,
    app_music_mp3_file_t *mp3_file,
    bool *out_reached_eof
)
{
    mp3d_sample_t *frame_buffer = NULL;
    mp3dec_frame_info_t frame_info = {0};
    size_t decoded_samples;

    decoded_samples = mp3dec_ex_read_frame(
        &decoder_state->decoder,
        &frame_buffer,
        &frame_info,
        MINIMP3_MAX_SAMPLES_PER_FRAME
    );

    if (decoded_samples == 0U) {
        if (decoder_state->decoder.last_error != 0) {
            app_music_mp3_set_last_detail(
                "read code=%d (%s)",
                decoder_state->decoder.last_error,
                app_music_mp3_code_to_text(decoder_state->decoder.last_error)
            );
            ESP_LOGE(
                TAG,
                "mp3 decode halted: code=%d (%s)",
                decoder_state->decoder.last_error,
                app_music_mp3_code_to_text(decoder_state->decoder.last_error)
            );
            return app_music_mp3_error_from_code(decoder_state->decoder.last_error);
        }
        if (out_reached_eof != NULL) {
            *out_reached_eof = true;
        }
        app_music_mp3_set_last_detail("eof");
        return ESP_OK;
    }

    if (frame_info.channels == 1 || frame_info.channels == 2) {
        decoder_state->last_channels = frame_info.channels;
    } else if (decoder_state->last_channels > 0) {
        frame_info.channels = decoder_state->last_channels;
    }
    if (frame_info.hz > 0) {
        decoder_state->last_hz = frame_info.hz;
    } else if (decoder_state->last_hz > 0) {
        frame_info.hz = decoder_state->last_hz;
    }
    if (frame_info.hz > 0 &&
        mp3_file->sample_rate_hz != 0U &&
        frame_info.hz != (int)mp3_file->sample_rate_hz) {
        app_music_mp3_set_last_detail(
            "sample rate change %lu->%d",
            (unsigned long)mp3_file->sample_rate_hz,
            frame_info.hz
        );
        return ESP_ERR_NOT_SUPPORTED;
    }
    if ((frame_info.channels == 1 || frame_info.channels == 2) &&
        mp3_file->channel_count != 0U &&
        frame_info.channels != (int)mp3_file->channel_count) {
        app_music_mp3_set_last_detail(
            "channel change %u->%d",
            (unsigned)mp3_file->channel_count,
            frame_info.channels
        );
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (frame_info.channels != 1 && frame_info.channels != 2) {
        app_music_mp3_set_last_detail("unsupported frame ch=%d", frame_info.channels);
        return ESP_ERR_NOT_SUPPORTED;
    }

    memcpy(decoder_state->primed_samples, frame_buffer, decoded_samples * sizeof(mp3d_sample_t));
    decoder_state->primed_sample_count = decoded_samples;
    decoder_state->primed_sample_offset = 0U;
    return ESP_OK;
}

esp_err_t app_music_mp3_open(const char *path, app_music_mp3_file_t *out_file)
{
    app_music_mp3_decoder_t *decoder_state;
    FILE *handle;
    int open_result;

    ESP_RETURN_ON_FALSE(path != NULL, ESP_ERR_INVALID_ARG, TAG, "path is null");
    ESP_RETURN_ON_FALSE(out_file != NULL, ESP_ERR_INVALID_ARG, TAG, "out_file is null");

    memset(out_file, 0, sizeof(*out_file));
    app_music_mp3_set_last_detail("none");
    handle = fopen(path, "rb");
    if (handle == NULL) {
        app_music_mp3_set_last_detail("fopen failed");
    }
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_NOT_FOUND, TAG, "failed to open %s", path);

    decoder_state = calloc(1, sizeof(*decoder_state));
    if (decoder_state == NULL) {
        app_music_mp3_set_last_detail("decoder alloc failed");
        fclose(handle);
        return ESP_ERR_NO_MEM;
    }

    decoder_state->io.read = app_music_mp3_read_cb;
    decoder_state->io.read_data = handle;
    decoder_state->io.seek = app_music_mp3_seek_cb;
    decoder_state->io.seek_data = handle;

    open_result = mp3dec_ex_open_cb(&decoder_state->decoder, &decoder_state->io, MP3D_DO_NOT_SCAN);
    if (open_result != 0) {
        app_music_mp3_set_last_detail("open code=%d (%s)", open_result, app_music_mp3_code_to_text(open_result));
        ESP_LOGE(TAG, "mp3 open failed for %s: code=%d (%s)", path, open_result, app_music_mp3_code_to_text(open_result));
        mp3dec_ex_close(&decoder_state->decoder);
        free(decoder_state);
        fclose(handle);
        return app_music_mp3_error_from_code(open_result);
    }

    out_file->handle = handle;
    out_file->decoder_state = decoder_state;
    out_file->sample_rate_hz = (uint32_t)decoder_state->decoder.info.hz;
    out_file->channel_count = (uint16_t)decoder_state->decoder.info.channels;
    if (out_file->sample_rate_hz == 0U ||
        (out_file->channel_count != 1U && out_file->channel_count != 2U)) {
        esp_err_t prime_err = app_music_mp3_prime_first_frame(decoder_state, out_file);
        if (prime_err != ESP_OK) {
            ESP_LOGE(
                TAG,
                "unsupported mp3 stream %s: hz=%d channels=%d detail=%s",
                path,
                decoder_state->decoder.info.hz,
                decoder_state->decoder.info.channels,
                app_music_mp3_get_last_detail()
            );
            mp3dec_ex_close(&decoder_state->decoder);
            free(decoder_state);
            fclose(handle);
            memset(out_file, 0, sizeof(*out_file));
            return prime_err;
        }
    } else {
        decoder_state->last_hz = (int)out_file->sample_rate_hz;
        decoder_state->last_channels = (int)out_file->channel_count;
    }
    app_music_mp3_set_last_detail(
        "open ok hz=%lu ch=%u",
        (unsigned long)out_file->sample_rate_hz,
        (unsigned)out_file->channel_count
    );

    ESP_LOGI(
        TAG,
        "opened mp3 %s: %lu Hz, %u ch",
        path,
        (unsigned long)out_file->sample_rate_hz,
        (unsigned)out_file->channel_count
    );
    return ESP_OK;
}

esp_err_t app_music_mp3_read_stereo_frames(
    app_music_mp3_file_t *mp3_file,
    int16_t *out_buffer,
    size_t max_frame_count,
    size_t *out_frame_count,
    bool *out_reached_eof
)
{
    app_music_mp3_decoder_t *decoder_state;
    size_t written_frames = 0;

    ESP_RETURN_ON_FALSE(mp3_file != NULL, ESP_ERR_INVALID_ARG, TAG, "mp3_file is null");
    ESP_RETURN_ON_FALSE(mp3_file->decoder_state != NULL, ESP_ERR_INVALID_STATE, TAG, "mp3 file not open");
    ESP_RETURN_ON_FALSE(out_buffer != NULL, ESP_ERR_INVALID_ARG, TAG, "out_buffer is null");

    if (out_frame_count != NULL) {
        *out_frame_count = 0;
    }
    if (out_reached_eof != NULL) {
        *out_reached_eof = false;
    }

    decoder_state = (app_music_mp3_decoder_t *)mp3_file->decoder_state;

    while (decoder_state->primed_sample_offset < decoder_state->primed_sample_count && written_frames < max_frame_count) {
        if (mp3_file->channel_count == 2U) {
            size_t available_samples = decoder_state->primed_sample_count - decoder_state->primed_sample_offset;
            size_t available_frames = available_samples / 2U;
            size_t frames_to_copy = available_frames;
            if (frames_to_copy > (max_frame_count - written_frames)) {
                frames_to_copy = max_frame_count - written_frames;
            }
            memcpy(
                out_buffer + (written_frames * 2U),
                decoder_state->primed_samples + decoder_state->primed_sample_offset,
                frames_to_copy * 2U * sizeof(int16_t)
            );
            decoder_state->primed_sample_offset += frames_to_copy * 2U;
            written_frames += frames_to_copy;
        } else if (mp3_file->channel_count == 1U) {
            size_t available_samples = decoder_state->primed_sample_count - decoder_state->primed_sample_offset;
            size_t frames_to_copy = available_samples;
            if (frames_to_copy > (max_frame_count - written_frames)) {
                frames_to_copy = max_frame_count - written_frames;
            }
            for (size_t i = 0; i < frames_to_copy; i++) {
                int16_t mono_sample = decoder_state->primed_samples[decoder_state->primed_sample_offset + i];
                out_buffer[written_frames * 2U] = mono_sample;
                out_buffer[(written_frames * 2U) + 1U] = mono_sample;
                written_frames++;
            }
            decoder_state->primed_sample_offset += frames_to_copy;
        } else {
            app_music_mp3_set_last_detail("primed invalid ch=%u", (unsigned)mp3_file->channel_count);
            return ESP_ERR_NOT_SUPPORTED;
        }
    }

    while (written_frames < max_frame_count) {
        if (decoder_state->primed_sample_offset >= decoder_state->primed_sample_count) {
            esp_err_t err = app_music_mp3_fill_cached_frame(decoder_state, mp3_file, out_reached_eof);
            if (err != ESP_OK) {
                return err;
            }
            if (decoder_state->primed_sample_count == 0U) {
                break;
            }
        }

        if (mp3_file->channel_count == 2U) {
            size_t available_samples = decoder_state->primed_sample_count - decoder_state->primed_sample_offset;
            size_t available_frames = available_samples / 2U;
            size_t frames_to_copy = available_frames;
            if (frames_to_copy > (max_frame_count - written_frames)) {
                frames_to_copy = max_frame_count - written_frames;
            }
            memcpy(
                out_buffer + (written_frames * 2U),
                decoder_state->primed_samples + decoder_state->primed_sample_offset,
                frames_to_copy * 2U * sizeof(int16_t)
            );
            decoder_state->primed_sample_offset += frames_to_copy * 2U;
            written_frames += frames_to_copy;
        } else if (mp3_file->channel_count == 1U) {
            size_t available_samples = decoder_state->primed_sample_count - decoder_state->primed_sample_offset;
            size_t frames_to_copy = available_samples;
            if (frames_to_copy > (max_frame_count - written_frames)) {
                frames_to_copy = max_frame_count - written_frames;
            }
            for (size_t i = 0; i < frames_to_copy; i++) {
                int16_t mono_sample = decoder_state->primed_samples[decoder_state->primed_sample_offset + i];
                out_buffer[written_frames * 2U] = mono_sample;
                out_buffer[(written_frames * 2U) + 1U] = mono_sample;
                written_frames++;
            }
            decoder_state->primed_sample_offset += frames_to_copy;
        } else {
            app_music_mp3_set_last_detail("cached invalid ch=%u", (unsigned)mp3_file->channel_count);
            return ESP_ERR_NOT_SUPPORTED;
        }
    }

    if (out_frame_count != NULL) {
        *out_frame_count = written_frames;
    }
    return ESP_OK;
}

void app_music_mp3_close(app_music_mp3_file_t *mp3_file)
{
    app_music_mp3_decoder_t *decoder_state;

    if (mp3_file == NULL) {
        return;
    }

    decoder_state = (app_music_mp3_decoder_t *)mp3_file->decoder_state;
    if (decoder_state != NULL) {
        mp3dec_ex_close(&decoder_state->decoder);
        free(decoder_state);
    }
    if (mp3_file->handle != NULL) {
        fclose(mp3_file->handle);
    }

    memset(mp3_file, 0, sizeof(*mp3_file));
}
