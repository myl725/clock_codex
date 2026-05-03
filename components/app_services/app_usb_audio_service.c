#include "app_usb_audio_service.h"

#include <math.h>
#include <string.h>

#include "app_usb_audio_descriptors.h"
#include "app_music_service.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tusb.h"

static const char *TAG = "app_usb_audio";
static SemaphoreHandle_t s_usb_audio_mutex;
static app_usb_audio_status_t s_usb_audio_status;
static bool s_usb_audio_driver_installed;
static TaskHandle_t s_usb_audio_task_handle;
static const uint32_t s_usb_audio_supported_sample_rates_hz[] = {44100U, APP_USB_AUDIO_SAMPLE_RATE_HZ};
static uint32_t s_usb_audio_current_sample_rate_hz = 44100U;
static int8_t s_usb_audio_mute[APP_USB_AUDIO_CHANNEL_COUNT + 1U];
static int16_t s_usb_audio_volume[APP_USB_AUDIO_CHANNEL_COUNT + 1U];

enum {
    APP_USB_AUDIO_VOLUME_0_DB = 0,
    APP_USB_AUDIO_VOLUME_50_DB = 12800,
    APP_USB_AUDIO_VOLUME_STEP_DB_256 = 512,
    APP_USB_AUDIO_STREAM_TASK_STACK_WORDS = 4096,
    APP_USB_AUDIO_STREAM_TASK_PRIORITY = 3,
    APP_USB_AUDIO_STREAM_TASK_CORE = 1,
    APP_USB_AUDIO_PACKET_LOG_INTERVAL = 256,
    APP_USB_AUDIO_STREAM_POLL_MS = 1,
};

static bool app_usb_audio_service_is_supported_sample_rate(uint32_t sample_rate_hz)
{
    size_t index = 0U;

    for (index = 0U; index < (sizeof(s_usb_audio_supported_sample_rates_hz) / sizeof(s_usb_audio_supported_sample_rates_hz[0])); index++) {
        if (s_usb_audio_supported_sample_rates_hz[index] == sample_rate_hz) {
            return true;
        }
    }

    return false;
}

static bool app_usb_audio_service_stream_is_active(void)
{
    bool stream_active = false;

    if (s_usb_audio_mutex == NULL) {
        return false;
    }

    xSemaphoreTake(s_usb_audio_mutex, portMAX_DELAY);
    stream_active = s_usb_audio_status.stream_active;
    xSemaphoreGive(s_usb_audio_mutex);
    return stream_active;
}

static int16_t app_usb_audio_service_clamp_volume_db_256(int32_t volume_db_256)
{
    if (volume_db_256 > APP_USB_AUDIO_VOLUME_0_DB) {
        return APP_USB_AUDIO_VOLUME_0_DB;
    }

    if (volume_db_256 < -APP_USB_AUDIO_VOLUME_50_DB) {
        return (int16_t)(-APP_USB_AUDIO_VOLUME_50_DB);
    }

    return (int16_t)volume_db_256;
}

static void app_usb_audio_service_set_master_volume_locked(int16_t volume_db_256)
{
    size_t channel = 0U;
    int16_t clamped = app_usb_audio_service_clamp_volume_db_256(volume_db_256);

    for (channel = 0U; channel < (APP_USB_AUDIO_CHANNEL_COUNT + 1U); channel++) {
        s_usb_audio_volume[channel] = clamped;
    }
    s_usb_audio_status.master_volume_db_256 = clamped;
}

static void app_usb_audio_service_set_master_mute_locked(bool mute)
{
    size_t channel = 0U;
    int8_t mute_value = mute ? 1 : 0;

    for (channel = 0U; channel < (APP_USB_AUDIO_CHANNEL_COUNT + 1U); channel++) {
        s_usb_audio_mute[channel] = mute_value;
    }
    s_usb_audio_status.master_mute = mute;
}

static void app_usb_audio_service_apply_master_gain(int16_t *samples, size_t frame_count)
{
    bool muted = false;
    int16_t master_volume_db_256 = 0;
    float gain = 1.0f;
    size_t sample_count = frame_count * APP_USB_AUDIO_CHANNEL_COUNT;
    size_t sample_index = 0U;

    if (samples == NULL || frame_count == 0U || s_usb_audio_mutex == NULL) {
        return;
    }

    xSemaphoreTake(s_usb_audio_mutex, portMAX_DELAY);
    muted = s_usb_audio_status.master_mute;
    master_volume_db_256 = s_usb_audio_status.master_volume_db_256;
    xSemaphoreGive(s_usb_audio_mutex);

    if (muted) {
        memset(samples, 0, sample_count * sizeof(int16_t));
        return;
    }

    if (master_volume_db_256 == APP_USB_AUDIO_VOLUME_0_DB) {
        return;
    }

    gain = powf(10.0f, ((float)master_volume_db_256 / 256.0f) / 20.0f);
    for (sample_index = 0U; sample_index < sample_count; sample_index++) {
        float scaled = (float)samples[sample_index] * gain;
        if (scaled > 32767.0f) {
            scaled = 32767.0f;
        } else if (scaled < -32768.0f) {
            scaled = -32768.0f;
        }
        samples[sample_index] = (int16_t)lrintf(scaled);
    }
}

static void app_usb_audio_service_log_control_request(
    const char *phase,
    const char *target,
    audio_control_request_t const *request
)
{
    if (phase == NULL || target == NULL || request == NULL) {
        return;
    }

    ESP_LOGD(
        TAG,
        "%s %s entity=%u selector=%u request=%u channel=%u itf=%u len=%u",
        phase,
        target,
        (unsigned)request->bEntityID,
        (unsigned)request->bControlSelector,
        (unsigned)request->bRequest,
        (unsigned)request->bChannelNumber,
        (unsigned)request->bInterface,
        (unsigned)request->wLength
    );
}

static esp_err_t app_usb_audio_service_ensure_init(void)
{
    if (s_usb_audio_mutex != NULL) {
        return ESP_OK;
    }

    s_usb_audio_mutex = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_usb_audio_mutex != NULL, ESP_ERR_NO_MEM, "", "failed to create usb audio mutex");

    xSemaphoreTake(s_usb_audio_mutex, portMAX_DELAY);
    memset(&s_usb_audio_status, 0, sizeof(s_usb_audio_status));
    s_usb_audio_status.ready = true;
    s_usb_audio_status.driver_installed = false;
    app_usb_audio_service_set_master_volume_locked(APP_USB_AUDIO_VOLUME_0_DB);
    app_usb_audio_service_set_master_mute_locked(false);
    xSemaphoreGive(s_usb_audio_mutex);
    return ESP_OK;
}

static void app_usb_audio_service_set_stream_state(bool active, uint32_t sample_rate_hz)
{
    if (s_usb_audio_mutex == NULL) {
        return;
    }

    xSemaphoreTake(s_usb_audio_mutex, portMAX_DELAY);
    s_usb_audio_status.stream_active = active;
    s_usb_audio_status.sample_rate_hz = active ? sample_rate_hz : 0U;
    if (active) {
        s_usb_audio_status.stream_begin_count++;
    } else {
        s_usb_audio_status.stream_end_count++;
        s_usb_audio_status.packet_count = 0U;
        s_usb_audio_status.accepted_frames = 0U;
        s_usb_audio_status.dropped_frames = 0U;
        s_usb_audio_status.last_packet_frames = 0U;
    }
    xSemaphoreGive(s_usb_audio_mutex);
}

static void app_usb_audio_service_log_stream_counters(bool force)
{
    app_usb_audio_status_t status = {0};
    app_music_usb_audio_status_t usb_music_status = {0};

    app_usb_audio_service_get_status(&status);
    app_music_service_usb_get_status(&usb_music_status);

    if (!force && status.packet_count == 0U) {
        return;
    }

    ESP_LOGD(
        TAG,
        "usb stream mounted=%d active=%d alt=%u rate=%lu packets=%lu accepted=%lu dropped=%lu read_errors=%lu buffer=%u/%u underrun=%lu overflow=%lu",
        status.mounted,
        status.stream_active,
        (unsigned)status.stream_alt_setting,
        (unsigned long)status.sample_rate_hz,
        (unsigned long)status.packet_count,
        (unsigned long)status.accepted_frames,
        (unsigned long)status.dropped_frames,
        (unsigned long)status.read_error_count,
        (unsigned)usb_music_status.buffered_frames,
        (unsigned)usb_music_status.capacity_frames,
        (unsigned long)usb_music_status.underrun_count,
        (unsigned long)usb_music_status.overflow_count
    );
}

static void app_usb_audio_service_event_cb(tinyusb_event_t *event, void *arg)
{
    (void)arg;

    if (s_usb_audio_mutex == NULL || event == NULL) {
        return;
    }

    xSemaphoreTake(s_usb_audio_mutex, portMAX_DELAY);
    switch (event->id) {
        case TINYUSB_EVENT_ATTACHED:
            s_usb_audio_status.mounted = true;
            break;
        case TINYUSB_EVENT_DETACHED:
            s_usb_audio_status.mounted = false;
            s_usb_audio_status.suspended = false;
            break;
#ifdef CONFIG_TINYUSB_SUSPEND_CALLBACK
        case TINYUSB_EVENT_SUSPENDED:
            s_usb_audio_status.suspended = true;
            break;
#endif
#ifdef CONFIG_TINYUSB_RESUME_CALLBACK
        case TINYUSB_EVENT_RESUMED:
            s_usb_audio_status.suspended = false;
            break;
#endif
        default:
            break;
    }
    xSemaphoreGive(s_usb_audio_mutex);

    switch (event->id) {
        case TINYUSB_EVENT_ATTACHED:
            ESP_LOGI(TAG, "tinyusb attached");
            break;
        case TINYUSB_EVENT_DETACHED:
            ESP_LOGI(TAG, "tinyusb detached");
            break;
#ifdef CONFIG_TINYUSB_SUSPEND_CALLBACK
        case TINYUSB_EVENT_SUSPENDED:
            ESP_LOGI(TAG, "tinyusb suspended");
            break;
#endif
#ifdef CONFIG_TINYUSB_RESUME_CALLBACK
        case TINYUSB_EVENT_RESUMED:
            ESP_LOGI(TAG, "tinyusb resumed");
            break;
#endif
        default:
            break;
    }
}

static void app_usb_audio_service_stream_task(void *arg)
{
    (void)arg;

    int16_t packet_buffer[CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX / sizeof(int16_t)] = {0};
    TickType_t last_wake_tick = xTaskGetTickCount();
    const TickType_t poll_ticks = pdMS_TO_TICKS(APP_USB_AUDIO_STREAM_POLL_MS) > 0 ?
        pdMS_TO_TICKS(APP_USB_AUDIO_STREAM_POLL_MS) : 1;
    const size_t frame_bytes = APP_USB_AUDIO_CHANNEL_COUNT * sizeof(int16_t);

    while (true) {
        if (s_usb_audio_driver_installed && tud_audio_mounted()) {
            uint32_t available_bytes = tud_audio_available();

            while (available_bytes >= frame_bytes) {
                size_t chunk_bytes = available_bytes;
                if (chunk_bytes > sizeof(packet_buffer)) {
                    chunk_bytes = sizeof(packet_buffer);
                }
                chunk_bytes -= (chunk_bytes % frame_bytes);
                if (chunk_bytes == 0U) {
                    break;
                }

                uint16_t byte_count = tud_audio_read(packet_buffer, chunk_bytes);
                size_t frame_count = byte_count / frame_bytes;

                if (frame_count > 0U) {
                    if (app_usb_audio_service_stream_is_active()) {
                        app_usb_audio_service_apply_master_gain(packet_buffer, frame_count);
                        size_t accepted_frames = 0U;
                        esp_err_t err = app_usb_audio_service_ingest_stereo(packet_buffer, frame_count, &accepted_frames);
                        if (err != ESP_OK) {
                            xSemaphoreTake(s_usb_audio_mutex, portMAX_DELAY);
                            s_usb_audio_status.read_error_count++;
                            xSemaphoreGive(s_usb_audio_mutex);
                            ESP_LOGW(TAG, "usb audio ingest failed: %s", esp_err_to_name(err));
                        } else {
                            uint32_t packet_count = 0U;
                            xSemaphoreTake(s_usb_audio_mutex, portMAX_DELAY);
                            packet_count = s_usb_audio_status.packet_count;
                            xSemaphoreGive(s_usb_audio_mutex);
                        if ((accepted_frames < frame_count) || ((packet_count % APP_USB_AUDIO_PACKET_LOG_INTERVAL) == 0U)) {
                            app_usb_audio_service_log_stream_counters(false);
                        }
                        }
                    }
                }

                if (byte_count < chunk_bytes) {
                    break;
                }

                available_bytes = tud_audio_available();
            }
        }

        vTaskDelayUntil(&last_wake_tick, poll_ticks);
    }
}

static tinyusb_config_t app_usb_audio_service_make_tinyusb_config(void)
{
    tinyusb_config_t tinyusb_cfg = TINYUSB_DEFAULT_CONFIG(app_usb_audio_service_event_cb, NULL);

    tinyusb_cfg.task = TINYUSB_TASK_CUSTOM(4096, 4, APP_USB_AUDIO_STREAM_TASK_CORE);
    tinyusb_cfg.descriptor = *app_usb_audio_descriptors_get_config();
    return tinyusb_cfg;
}

esp_err_t app_usb_audio_service_init(void)
{
    ESP_RETURN_ON_ERROR(app_usb_audio_service_ensure_init(), "", "usb audio init failed");

    if (s_usb_audio_driver_installed) {
        return ESP_OK;
    }

    tinyusb_config_t tinyusb_cfg = app_usb_audio_service_make_tinyusb_config();
    ESP_RETURN_ON_ERROR(tinyusb_driver_install(&tinyusb_cfg), TAG, "failed to install tinyusb driver");

    BaseType_t task_ok = xTaskCreatePinnedToCore(
        app_usb_audio_service_stream_task,
        "usb_audio_task",
        APP_USB_AUDIO_STREAM_TASK_STACK_WORDS,
        NULL,
        APP_USB_AUDIO_STREAM_TASK_PRIORITY,
        &s_usb_audio_task_handle,
        APP_USB_AUDIO_STREAM_TASK_CORE
    );
    ESP_RETURN_ON_FALSE(task_ok == pdPASS, ESP_ERR_NO_MEM, TAG, "failed to create usb audio task");

    s_usb_audio_driver_installed = true;
    xSemaphoreTake(s_usb_audio_mutex, portMAX_DELAY);
    s_usb_audio_status.driver_installed = true;
    xSemaphoreGive(s_usb_audio_mutex);
    ESP_LOGI(
        TAG,
        "tinyusb audio driver installed, target format: 44.1/48kHz stereo 16-bit, tinyusb_core=%d worker_core=%d worker_prio=%d",
        APP_USB_AUDIO_STREAM_TASK_CORE,
        APP_USB_AUDIO_STREAM_TASK_CORE,
        APP_USB_AUDIO_STREAM_TASK_PRIORITY
    );

    return ESP_OK;
}

esp_err_t app_usb_audio_service_begin_stream(uint32_t sample_rate_hz)
{
    ESP_RETURN_ON_FALSE(sample_rate_hz > 0U, ESP_ERR_INVALID_ARG, "", "sample rate must be positive");
    ESP_RETURN_ON_ERROR(app_usb_audio_service_ensure_init(), "", "usb audio init failed");

    ESP_RETURN_ON_ERROR(app_music_service_set_source(APP_MUSIC_SOURCE_USB_AUDIO), "", "failed to select usb source");
    ESP_RETURN_ON_ERROR(app_music_service_usb_begin(sample_rate_hz), "", "failed to begin usb music stream");
    ESP_RETURN_ON_ERROR(app_music_service_play(), "", "failed to start usb playback");
    app_usb_audio_service_set_stream_state(true, sample_rate_hz);
    ESP_LOGI(TAG, "usb stream started at %lu Hz", (unsigned long)sample_rate_hz);
    return ESP_OK;
}

esp_err_t app_usb_audio_service_ingest_stereo(
    const int16_t *samples,
    size_t frame_count,
    size_t *out_accepted_frames
)
{
    size_t accepted_frames = 0U;

    ESP_RETURN_ON_FALSE(samples != NULL, ESP_ERR_INVALID_ARG, "", "samples is null");
    ESP_RETURN_ON_FALSE(frame_count > 0U, ESP_ERR_INVALID_ARG, "", "frame count must be positive");
    ESP_RETURN_ON_ERROR(app_usb_audio_service_ensure_init(), "", "usb audio init failed");

    xSemaphoreTake(s_usb_audio_mutex, portMAX_DELAY);
    bool stream_active = s_usb_audio_status.stream_active;
    xSemaphoreGive(s_usb_audio_mutex);
    ESP_RETURN_ON_FALSE(stream_active, ESP_ERR_INVALID_STATE, "", "usb stream is not active");

    ESP_RETURN_ON_ERROR(
        app_music_service_usb_push_stereo(samples, frame_count, &accepted_frames),
        "",
        "failed to push usb audio frames"
    );

    xSemaphoreTake(s_usb_audio_mutex, portMAX_DELAY);
    s_usb_audio_status.packet_count++;
    s_usb_audio_status.accepted_frames += (uint32_t)accepted_frames;
    s_usb_audio_status.dropped_frames += (uint32_t)(frame_count - accepted_frames);
    s_usb_audio_status.last_packet_frames = (uint32_t)frame_count;
    xSemaphoreGive(s_usb_audio_mutex);

    if (out_accepted_frames != NULL) {
        *out_accepted_frames = accepted_frames;
    }
    return ESP_OK;
}

void app_usb_audio_service_end_stream(void)
{
    if (s_usb_audio_mutex == NULL) {
        return;
    }

    app_usb_audio_service_set_stream_state(false, 0U);
    app_music_service_usb_end();
    app_music_service_stop();
    app_usb_audio_service_log_stream_counters(true);
    ESP_LOGI(TAG, "usb stream stopped");
}

void app_usb_audio_service_get_status(app_usb_audio_status_t *out_status)
{
    if (out_status == NULL) {
        return;
    }

    memset(out_status, 0, sizeof(*out_status));
    if (s_usb_audio_mutex == NULL) {
        return;
    }

    xSemaphoreTake(s_usb_audio_mutex, portMAX_DELAY);
    *out_status = s_usb_audio_status;
    xSemaphoreGive(s_usb_audio_mutex);
}

void app_usb_audio_service_adjust_master_volume(int16_t delta_db_256)
{
    if (s_usb_audio_mutex == NULL) {
        return;
    }

    xSemaphoreTake(s_usb_audio_mutex, portMAX_DELAY);
    app_usb_audio_service_set_master_volume_locked((int16_t)(s_usb_audio_status.master_volume_db_256 + delta_db_256));
    xSemaphoreGive(s_usb_audio_mutex);
}

void app_usb_audio_service_toggle_master_mute(void)
{
    if (s_usb_audio_mutex == NULL) {
        return;
    }

    xSemaphoreTake(s_usb_audio_mutex, portMAX_DELAY);
    app_usb_audio_service_set_master_mute_locked(!s_usb_audio_status.master_mute);
    xSemaphoreGive(s_usb_audio_mutex);
}

bool tud_audio_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request)
{
    audio_control_request_t const *request = (audio_control_request_t const *)p_request;

    app_usb_audio_service_log_control_request("GET", "entity", request);

    if (request->bEntityID == APP_USB_AUDIO_ENTITY_CLOCK) {
        if (request->bControlSelector == AUDIO_CS_CTRL_SAM_FREQ) {
            if (request->bRequest == AUDIO_CS_REQ_CUR) {
                audio_control_cur_4_t current_frequency = {
                    .bCur = (int32_t)tu_htole32(s_usb_audio_current_sample_rate_hz),
                };
                return tud_audio_buffer_and_schedule_control_xfer(
                    rhport,
                    p_request,
                    &current_frequency,
                    sizeof(current_frequency)
                );
            }
            if (request->bRequest == AUDIO_CS_REQ_RANGE) {
                audio_control_range_4_n_t(2) range_frequency = {
                    .wNumSubRanges = tu_htole16(2),
                    .subrange[0] = {
                        .bMin = (int32_t)s_usb_audio_supported_sample_rates_hz[0],
                        .bMax = (int32_t)s_usb_audio_supported_sample_rates_hz[0],
                        .bRes = 0,
                    },
                    .subrange[1] = {
                        .bMin = (int32_t)s_usb_audio_supported_sample_rates_hz[1],
                        .bMax = (int32_t)s_usb_audio_supported_sample_rates_hz[1],
                        .bRes = 0,
                    }
                };
                return tud_audio_buffer_and_schedule_control_xfer(
                    rhport,
                    p_request,
                    &range_frequency,
                    sizeof(range_frequency)
                );
            }
        }

        if (request->bControlSelector == AUDIO_CS_CTRL_CLK_VALID &&
            request->bRequest == AUDIO_CS_REQ_CUR) {
            audio_control_cur_1_t clock_valid = {.bCur = 1};
            return tud_audio_buffer_and_schedule_control_xfer(
                rhport,
                p_request,
                &clock_valid,
                sizeof(clock_valid)
            );
        }
    }

    if (request->bEntityID == APP_USB_AUDIO_ENTITY_FEATURE_UNIT) {
        if (request->bControlSelector == AUDIO_FU_CTRL_MUTE &&
            request->bRequest == AUDIO_CS_REQ_CUR) {
            audio_control_cur_1_t mute_state = {.bCur = s_usb_audio_mute[request->bChannelNumber]};
            return tud_audio_buffer_and_schedule_control_xfer(
                rhport,
                p_request,
                &mute_state,
                sizeof(mute_state)
            );
        }

        if (request->bControlSelector == AUDIO_FU_CTRL_VOLUME) {
            if (request->bRequest == AUDIO_CS_REQ_RANGE) {
                audio_control_range_2_n_t(1) volume_range = {
                    .wNumSubRanges = tu_htole16(1),
                    .subrange[0] = {
                        .bMin = tu_htole16(-APP_USB_AUDIO_VOLUME_50_DB),
                        .bMax = tu_htole16(APP_USB_AUDIO_VOLUME_0_DB),
                        .bRes = tu_htole16(256),
                    },
                };
                return tud_audio_buffer_and_schedule_control_xfer(
                    rhport,
                    p_request,
                    &volume_range,
                    sizeof(volume_range)
                );
            }

            if (request->bRequest == AUDIO_CS_REQ_CUR) {
                audio_control_cur_2_t current_volume = {
                    .bCur = tu_htole16(s_usb_audio_volume[request->bChannelNumber]),
                };
                return tud_audio_buffer_and_schedule_control_xfer(
                    rhport,
                    p_request,
                    &current_volume,
                    sizeof(current_volume)
                );
            }
        }
    }

    ESP_LOGW(
        TAG,
        "unsupported GET entity request entity=%u selector=%u request=%u channel=%u",
        (unsigned)request->bEntityID,
        (unsigned)request->bControlSelector,
        (unsigned)request->bRequest,
        (unsigned)request->bChannelNumber
    );
    return false;
}

bool tud_audio_set_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *buffer)
{
    (void)rhport;

    audio_control_request_t const *request = (audio_control_request_t const *)p_request;

    app_usb_audio_service_log_control_request("SET", "entity", request);

    if (request->bEntityID == APP_USB_AUDIO_ENTITY_CLOCK &&
        request->bControlSelector == AUDIO_CS_CTRL_SAM_FREQ &&
        request->bRequest == AUDIO_CS_REQ_CUR) {
        uint32_t requested_sample_rate_hz = 0U;

        TU_VERIFY(request->wLength == sizeof(audio_control_cur_4_t));
        requested_sample_rate_hz = (uint32_t)((audio_control_cur_4_t const *)buffer)->bCur;
        TU_VERIFY(app_usb_audio_service_is_supported_sample_rate(requested_sample_rate_hz));
        s_usb_audio_current_sample_rate_hz = requested_sample_rate_hz;
        return true;
    }

    if (request->bEntityID == APP_USB_AUDIO_ENTITY_FEATURE_UNIT &&
        request->bRequest == AUDIO_CS_REQ_CUR) {
        if (request->bControlSelector == AUDIO_FU_CTRL_MUTE) {
            TU_VERIFY(request->wLength == sizeof(audio_control_cur_1_t));
            xSemaphoreTake(s_usb_audio_mutex, portMAX_DELAY);
            if (request->bChannelNumber == 0U) {
                app_usb_audio_service_set_master_mute_locked(((audio_control_cur_1_t const *)buffer)->bCur != 0);
            } else {
                s_usb_audio_mute[request->bChannelNumber] = ((audio_control_cur_1_t const *)buffer)->bCur;
            }
            xSemaphoreGive(s_usb_audio_mutex);
            return true;
        }

        if (request->bControlSelector == AUDIO_FU_CTRL_VOLUME) {
            TU_VERIFY(request->wLength == sizeof(audio_control_cur_2_t));
            xSemaphoreTake(s_usb_audio_mutex, portMAX_DELAY);
            if (request->bChannelNumber == 0U) {
                app_usb_audio_service_set_master_volume_locked(((audio_control_cur_2_t const *)buffer)->bCur);
            } else {
                s_usb_audio_volume[request->bChannelNumber] = ((audio_control_cur_2_t const *)buffer)->bCur;
            }
            xSemaphoreGive(s_usb_audio_mutex);
            return true;
        }
    }

    ESP_LOGW(
        TAG,
        "unsupported SET entity request entity=%u selector=%u request=%u channel=%u",
        (unsigned)request->bEntityID,
        (unsigned)request->bControlSelector,
        (unsigned)request->bRequest,
        (unsigned)request->bChannelNumber
    );
    return false;
}

bool tud_audio_get_req_itf_cb(uint8_t rhport, tusb_control_request_t const *p_request)
{
    (void)rhport;

    audio_control_request_t const *request = (audio_control_request_t const *)p_request;
    app_usb_audio_service_log_control_request("GET", "interface", request);
    ESP_LOGW(TAG, "unsupported GET interface request");
    return false;
}

bool tud_audio_set_req_itf_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *buffer)
{
    (void)rhport;
    (void)buffer;

    audio_control_request_t const *request = (audio_control_request_t const *)p_request;
    app_usb_audio_service_log_control_request("SET", "interface", request);
    ESP_LOGW(TAG, "unsupported SET interface request");
    return false;
}

bool tud_audio_get_req_ep_cb(uint8_t rhport, tusb_control_request_t const *p_request)
{
    (void)rhport;

    audio_control_request_t const *request = (audio_control_request_t const *)p_request;
    app_usb_audio_service_log_control_request("GET", "endpoint", request);
    ESP_LOGW(TAG, "unsupported GET endpoint request");
    return false;
}

bool tud_audio_set_req_ep_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *buffer)
{
    (void)rhport;
    (void)buffer;

    audio_control_request_t const *request = (audio_control_request_t const *)p_request;
    app_usb_audio_service_log_control_request("SET", "endpoint", request);
    ESP_LOGW(TAG, "unsupported SET endpoint request");
    return false;
}

bool tud_audio_set_itf_close_ep_cb(uint8_t rhport, tusb_control_request_t const *p_request)
{
    (void)rhport;

    uint8_t interface_number = tu_u16_low(tu_le16toh(p_request->wIndex));
    uint8_t alternate_setting = tu_u16_low(tu_le16toh(p_request->wValue));

    if (s_usb_audio_mutex != NULL) {
        xSemaphoreTake(s_usb_audio_mutex, portMAX_DELAY);
        s_usb_audio_status.stream_alt_setting = alternate_setting;
        xSemaphoreGive(s_usb_audio_mutex);
    }

    ESP_LOGI(TAG, "set interface close_ep itf=%u alt=%u", (unsigned)interface_number, (unsigned)alternate_setting);
    if (interface_number == APP_USB_AUDIO_ITF_AUDIO_STREAMING && alternate_setting == 0U) {
        app_usb_audio_service_end_stream();
    }

    return true;
}

bool tud_audio_set_itf_cb(uint8_t rhport, tusb_control_request_t const *p_request)
{
    (void)rhport;

    uint8_t interface_number = tu_u16_low(tu_le16toh(p_request->wIndex));
    uint8_t alternate_setting = tu_u16_low(tu_le16toh(p_request->wValue));

    if (s_usb_audio_mutex != NULL) {
        xSemaphoreTake(s_usb_audio_mutex, portMAX_DELAY);
        s_usb_audio_status.stream_alt_setting = alternate_setting;
        xSemaphoreGive(s_usb_audio_mutex);
    }

    ESP_LOGI(TAG, "set interface itf=%u alt=%u", (unsigned)interface_number, (unsigned)alternate_setting);
    if (interface_number == APP_USB_AUDIO_ITF_AUDIO_STREAMING && alternate_setting != 0U) {
        esp_err_t err = app_usb_audio_service_begin_stream(s_usb_audio_current_sample_rate_hz);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "failed to begin usb audio stream: %s", esp_err_to_name(err));
        }
    }

    return true;
}

#if CFG_TUD_AUDIO_ENABLE_EP_OUT && CFG_TUD_AUDIO_ENABLE_FEEDBACK_EP
void tud_audio_feedback_params_cb(uint8_t func_id, uint8_t alt_itf, audio_feedback_params_t *feedback_param)
{
    (void)func_id;
    (void)alt_itf;

    if (feedback_param == NULL) {
        return;
    }

    feedback_param->method = AUDIO_FEEDBACK_METHOD_FIFO_COUNT;
    feedback_param->sample_freq = s_usb_audio_current_sample_rate_hz;
}
#endif
