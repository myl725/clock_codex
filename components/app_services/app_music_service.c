#include "app_music_service.h"

#include <dirent.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>

#include "app_config.h"
#include "app_music_download.h"
#include "app_music_playback_source.h"
#include "app_music_source.h"
#include "app_music_usb_stream.h"
#include "audio_output.h"
#include "board_config.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"

#define APP_MUSIC_BUFFER_FRAMES 480
#define APP_MUSIC_VOLUME_PERCENT 14
#define APP_MUSIC_TIMEOUT_MS 500
#define APP_MUSIC_DIAG_MAGIC 0x4D555349UL
#define APP_MUSIC_MAX_WAV_FILES 8
#define APP_MUSIC_USB_START_THRESHOLD_FRAMES (APP_MUSIC_BUFFER_FRAMES * 2U)

typedef enum {
    APP_MUSIC_DIAG_STAGE_UNKNOWN = 0,
    APP_MUSIC_DIAG_STAGE_BOOT,
    APP_MUSIC_DIAG_STAGE_INIT_SERVICE,
    APP_MUSIC_DIAG_STAGE_INIT_AUDIO_OUTPUT,
    APP_MUSIC_DIAG_STAGE_CREATE_TASK,
    APP_MUSIC_DIAG_STAGE_IDLE,
    APP_MUSIC_DIAG_STAGE_PLAY_REQUEST,
    APP_MUSIC_DIAG_STAGE_STOP_REQUEST,
    APP_MUSIC_DIAG_STAGE_TOGGLE_REQUEST,
    APP_MUSIC_DIAG_STAGE_TONE_CHANGE,
    APP_MUSIC_DIAG_STAGE_START_OUTPUT,
    APP_MUSIC_DIAG_STAGE_FILL_BUFFER,
    APP_MUSIC_DIAG_STAGE_WRITE_BUFFER,
    APP_MUSIC_DIAG_STAGE_STOP_OUTPUT,
    APP_MUSIC_DIAG_STAGE_ERROR,
} app_music_diag_stage_t;

typedef struct {
    const char *name;
    float frequency_hz;
} app_music_tone_entry_t;

typedef struct {
    uint32_t magic;
    uint32_t boot_count;
    uint32_t mark_count;
    uint32_t successful_writes;
    int32_t current_error;
    int32_t last_failure_error;
    uint32_t sample_rate_hz;
    uint8_t volume_percent;
    uint8_t tone;
    uint8_t state;
    uint8_t stage;
    uint8_t last_failure_stage;
} app_music_diag_snapshot_t;

static const char *TAG = "app_music_service";

static const app_music_tone_entry_t TONE_TABLE[APP_MUSIC_TONE_COUNT] = {
    [APP_MUSIC_TONE_A4] = {"A4 440Hz", 440.0f},
    [APP_MUSIC_TONE_C5] = {"C5 523Hz", 523.25f},
    [APP_MUSIC_TONE_E5] = {"E5 659Hz", 659.25f},
};

static SemaphoreHandle_t s_music_mutex;
static app_music_snapshot_t s_snapshot;
static bool s_initialized;
static bool s_sd_mounted;
static sdmmc_card_t *s_sd_card;
static RTC_NOINIT_ATTR app_music_diag_snapshot_t s_music_diag;
static uint32_t s_render_loop_count;
static char s_wav_paths[APP_MUSIC_MAX_WAV_FILES][APP_MUSIC_WAV_PATH_LENGTH];
static uint8_t s_wav_file_count;
static uint8_t s_wav_selected_index;

const char *app_music_service_diag_stage_to_text(uint8_t stage_value)
{
    app_music_diag_stage_t stage = (app_music_diag_stage_t)stage_value;
    switch (stage) {
        case APP_MUSIC_DIAG_STAGE_BOOT:
            return "boot";
        case APP_MUSIC_DIAG_STAGE_INIT_SERVICE:
            return "init_service";
        case APP_MUSIC_DIAG_STAGE_INIT_AUDIO_OUTPUT:
            return "init_audio_output";
        case APP_MUSIC_DIAG_STAGE_CREATE_TASK:
            return "create_task";
        case APP_MUSIC_DIAG_STAGE_IDLE:
            return "idle";
        case APP_MUSIC_DIAG_STAGE_PLAY_REQUEST:
            return "play_request";
        case APP_MUSIC_DIAG_STAGE_STOP_REQUEST:
            return "stop_request";
        case APP_MUSIC_DIAG_STAGE_TOGGLE_REQUEST:
            return "toggle_request";
        case APP_MUSIC_DIAG_STAGE_TONE_CHANGE:
            return "tone_change";
        case APP_MUSIC_DIAG_STAGE_START_OUTPUT:
            return "start_output";
        case APP_MUSIC_DIAG_STAGE_FILL_BUFFER:
            return "fill_buffer";
        case APP_MUSIC_DIAG_STAGE_WRITE_BUFFER:
            return "write_buffer";
        case APP_MUSIC_DIAG_STAGE_STOP_OUTPUT:
            return "stop_output";
        case APP_MUSIC_DIAG_STAGE_ERROR:
            return "error";
        default:
            return "unknown";
    }
}

static const char *app_music_service_reset_reason_to_text(esp_reset_reason_t reason)
{
    switch (reason) {
        case ESP_RST_UNKNOWN:
            return "unknown";
        case ESP_RST_POWERON:
            return "poweron";
        case ESP_RST_EXT:
            return "external";
        case ESP_RST_SW:
            return "software";
        case ESP_RST_PANIC:
            return "panic";
        case ESP_RST_INT_WDT:
            return "int_wdt";
        case ESP_RST_TASK_WDT:
            return "task_wdt";
        case ESP_RST_WDT:
            return "wdt";
        case ESP_RST_DEEPSLEEP:
            return "deepsleep";
        case ESP_RST_BROWNOUT:
            return "brownout";
        case ESP_RST_SDIO:
            return "sdio";
        case ESP_RST_USB:
            return "usb";
        case ESP_RST_JTAG:
            return "jtag";
        case ESP_RST_EFUSE:
            return "efuse";
        case ESP_RST_PWR_GLITCH:
            return "power_glitch";
        case ESP_RST_CPU_LOCKUP:
            return "cpu_lockup";
        default:
            return "other";
    }
}

static void app_music_service_diag_init_if_needed(void)
{
    if (s_music_diag.magic == APP_MUSIC_DIAG_MAGIC) {
        return;
    }

    memset(&s_music_diag, 0, sizeof(s_music_diag));
    s_music_diag.magic = APP_MUSIC_DIAG_MAGIC;
}

static void app_music_service_diag_mark(app_music_diag_stage_t stage, esp_err_t err)
{
    app_music_service_diag_init_if_needed();

    s_music_diag.mark_count++;
    s_music_diag.stage = (uint8_t)stage;
    s_music_diag.current_error = (int32_t)err;
    if (err != ESP_OK) {
        s_music_diag.last_failure_error = (int32_t)err;
        s_music_diag.last_failure_stage = (uint8_t)stage;
    }

    if (s_music_mutex != NULL) {
        xSemaphoreTake(s_music_mutex, portMAX_DELAY);
        s_music_diag.sample_rate_hz = s_snapshot.sample_rate_hz;
        s_music_diag.volume_percent = s_snapshot.volume_percent;
        s_music_diag.tone = (uint8_t)s_snapshot.tone;
        s_music_diag.state = (uint8_t)s_snapshot.state;
        xSemaphoreGive(s_music_mutex);
    }
}

static void app_music_service_set_state(app_music_state_t state)
{
    if (s_music_mutex == NULL) {
        return;
    }

    xSemaphoreTake(s_music_mutex, portMAX_DELAY);
    s_snapshot.state = state;
    xSemaphoreGive(s_music_mutex);
}

static void app_music_service_set_status_text(const char *text)
{
    if (text == NULL || s_music_mutex == NULL) {
        return;
    }

    xSemaphoreTake(s_music_mutex, portMAX_DELAY);
    strncpy(s_snapshot.status_text, text, sizeof(s_snapshot.status_text) - 1U);
    s_snapshot.status_text[sizeof(s_snapshot.status_text) - 1U] = '\0';
    xSemaphoreGive(s_music_mutex);
}

static void app_music_service_set_status_text_fmt(const char *format, ...)
{
    va_list args;
    char buffer[sizeof(s_snapshot.status_text)];

    if (format == NULL || s_music_mutex == NULL) {
        return;
    }

    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    app_music_service_set_status_text(buffer);
}

static void app_music_service_download_status_cb(const char *status_text, void *ctx)
{
    (void)ctx;
    app_music_service_set_status_text(status_text);
}

static esp_err_t app_music_service_mount_sd_if_needed(void);

static esp_err_t app_music_service_mount_sd_action(void *ctx)
{
    (void)ctx;
    return app_music_service_mount_sd_if_needed();
}

static esp_err_t app_music_service_refresh_sd_listing_action(void *ctx)
{
    (void)ctx;
    return app_music_service_refresh_wav_files();
}

static void app_music_service_reset_playback_counters(app_music_diag_stage_t stage)
{
    s_music_diag.successful_writes = 0;
    s_music_diag.current_error = ESP_OK;
    s_music_diag.stage = (uint8_t)stage;
    s_render_loop_count = 0;
}

static bool app_music_service_source_is_valid(app_music_source_t source)
{
    return source >= APP_MUSIC_SOURCE_TONE && source < APP_MUSIC_SOURCE_COUNT;
}

static bool app_music_service_tone_is_valid(app_music_tone_t tone)
{
    return tone >= APP_MUSIC_TONE_A4 && tone < APP_MUSIC_TONE_COUNT;
}

static bool app_music_service_sd_is_configured(void)
{
    return BOARD_SD_SPI_CS_GPIO != GPIO_NUM_NC;
}

static const char *app_music_service_basename(const char *path)
{
    const char *basename = strrchr(path, '/');
    return basename == NULL ? path : basename + 1;
}

static bool app_music_service_has_audio_extension(const char *name)
{
    const char *extension = strrchr(name, '.');

    if (extension == NULL) {
        return false;
    }

    return strcasecmp(extension, ".wav") == 0 || strcasecmp(extension, ".mp3") == 0;
}

static void app_music_service_apply_selected_wav_locked(void)
{
    if (s_wav_file_count == 0U || s_wav_selected_index >= s_wav_file_count) {
        strncpy(s_snapshot.wav_path, APP_MUSIC_DEFAULT_WAV_PATH, sizeof(s_snapshot.wav_path) - 1U);
        s_snapshot.wav_path[sizeof(s_snapshot.wav_path) - 1U] = '\0';
        return;
    }

    strncpy(s_snapshot.wav_path, s_wav_paths[s_wav_selected_index], sizeof(s_snapshot.wav_path) - 1U);
    s_snapshot.wav_path[sizeof(s_snapshot.wav_path) - 1U] = '\0';
}

static void app_music_service_set_snapshot_sample_rate(uint32_t sample_rate_hz)
{
    if (s_music_mutex == NULL) {
        return;
    }

    xSemaphoreTake(s_music_mutex, portMAX_DELAY);
    s_snapshot.sample_rate_hz = sample_rate_hz;
    xSemaphoreGive(s_music_mutex);
}

static esp_err_t app_music_service_mount_sd_if_needed(void)
{
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = BOARD_SD_SPI_MOSI_GPIO,
        .miso_io_num = BOARD_SD_SPI_MISO_GPIO,
        .sclk_io_num = BOARD_SD_SPI_CLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = false,
        .use_one_fat = false,
    };
    bool bus_owned = false;
    esp_err_t err;

    if (s_sd_mounted) {
        return ESP_OK;
    }
    ESP_RETURN_ON_FALSE(app_music_service_sd_is_configured(), ESP_ERR_NOT_SUPPORTED, TAG, "sd cs pin is not configured");

    app_music_service_set_status_text("Mounting SD card");
    err = spi_bus_initialize(BOARD_SD_SPI_HOST, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (err == ESP_OK) {
        bus_owned = true;
    } else if (err != ESP_ERR_INVALID_STATE) {
        app_music_service_set_status_text_fmt("SD SPI init failed: %s", esp_err_to_name(err));
        return err;
    }

    host.slot = BOARD_SD_SPI_HOST;
    slot_config.host_id = BOARD_SD_SPI_HOST;
    slot_config.gpio_cs = BOARD_SD_SPI_CS_GPIO;

    err = esp_vfs_fat_sdspi_mount(APP_MUSIC_SD_MOUNT_POINT, &host, &slot_config, &mount_config, &s_sd_card);
    if (err != ESP_OK) {
        app_music_service_set_status_text_fmt("SD mount failed: %s", esp_err_to_name(err));
        if (bus_owned) {
            spi_bus_free(BOARD_SD_SPI_HOST);
        }
        return err;
    }

    s_sd_mounted = true;
    app_music_service_set_status_text("SD card mounted");
    ESP_LOGI(TAG, "SD mounted at %s", APP_MUSIC_SD_MOUNT_POINT);
    return ESP_OK;
}

esp_err_t app_music_service_refresh_wav_files(void)
{
    char scanned_paths[APP_MUSIC_MAX_WAV_FILES][APP_MUSIC_WAV_PATH_LENGTH] = {0};
    char current_path[APP_MUSIC_WAV_PATH_LENGTH] = {0};
    uint8_t scanned_count = 0;
    DIR *directory;
    struct dirent *entry;
    esp_err_t err;

    ESP_RETURN_ON_FALSE(s_music_mutex != NULL, ESP_ERR_INVALID_STATE, TAG, "music service not initialized");

    err = app_music_service_mount_sd_if_needed();
    if (err != ESP_OK) {
        xSemaphoreTake(s_music_mutex, portMAX_DELAY);
        s_wav_file_count = 0;
        s_wav_selected_index = 0;
        app_music_service_apply_selected_wav_locked();
        xSemaphoreGive(s_music_mutex);
        return err;
    }

    directory = opendir(APP_MUSIC_SD_MOUNT_POINT);
    if (directory == NULL) {
        app_music_service_set_status_text("Failed to open SD directory");
        return ESP_ERR_NOT_FOUND;
    }

    while ((entry = readdir(directory)) != NULL && scanned_count < APP_MUSIC_MAX_WAV_FILES) {
        if (entry->d_name[0] == '\0' || entry->d_name[0] == '.') {
            continue;
        }
        if (!app_music_service_has_audio_extension(entry->d_name)) {
            continue;
        }

        int written = snprintf(
            scanned_paths[scanned_count],
            sizeof(scanned_paths[scanned_count]),
            "%s/%s",
            APP_MUSIC_SD_MOUNT_POINT,
            entry->d_name
        );
        if (written < 0 || (size_t)written >= sizeof(scanned_paths[scanned_count])) {
            ESP_LOGW(TAG, "skipping long audio path: %s", entry->d_name);
            continue;
        }
        scanned_count++;
    }
    closedir(directory);

    xSemaphoreTake(s_music_mutex, portMAX_DELAY);
    strncpy(current_path, s_snapshot.wav_path, sizeof(current_path) - 1U);
    memset(s_wav_paths, 0, sizeof(s_wav_paths));
    for (uint8_t i = 0; i < scanned_count; i++) {
        strncpy(s_wav_paths[i], scanned_paths[i], sizeof(s_wav_paths[i]) - 1U);
        s_wav_paths[i][sizeof(s_wav_paths[i]) - 1U] = '\0';
    }
    s_wav_file_count = scanned_count;
    if (scanned_count == 0U) {
        s_wav_selected_index = 0;
    } else {
        uint8_t selected_index = 0;
        bool found_selected = false;
        for (uint8_t i = 0; i < scanned_count; i++) {
            if (strcmp(scanned_paths[i], current_path) == 0) {
                selected_index = i;
                found_selected = true;
                break;
            }
        }
        s_wav_selected_index = found_selected ? selected_index : 0U;
    }
    app_music_service_apply_selected_wav_locked();
    xSemaphoreGive(s_music_mutex);

    if (scanned_count == 0U) {
        app_music_service_set_status_text("No audio files found on SD");
    } else {
        app_music_service_set_status_text_fmt(
            "Found %u audio file%s",
            (unsigned)scanned_count,
            scanned_count == 1U ? "" : "s"
        );
    }

    return ESP_OK;
}

static esp_err_t app_music_service_configure_output_for_source(
    app_music_playback_source_t *active_source,
    uint32_t *out_sample_rate_hz
)
{
    esp_err_t err;
    uint32_t sample_rate_hz;

    ESP_RETURN_ON_FALSE(active_source != NULL, ESP_ERR_INVALID_ARG, TAG, "active_source is null");

    sample_rate_hz = app_music_playback_source_get_sample_rate(active_source);
    err = audio_output_set_sample_rate(sample_rate_hz);
    if (err != ESP_OK) {
        return err;
    }

    app_music_service_set_snapshot_sample_rate(sample_rate_hz);
    if (out_sample_rate_hz != NULL) {
        *out_sample_rate_hz = sample_rate_hz;
    }
    return ESP_OK;
}

static uint32_t app_music_service_default_sample_rate_for_source(app_music_source_t source)
{
    if (source == APP_MUSIC_SOURCE_USB_AUDIO) {
        uint32_t usb_rate_hz = app_music_usb_stream_get_sample_rate();
        return usb_rate_hz > 0U ? usb_rate_hz : 48000U;
    }

    return BOARD_AUDIO_OUTPUT_SAMPLE_RATE;
}

static const char *app_music_service_starting_status_text(app_music_source_t source)
{
    switch (source) {
        case APP_MUSIC_SOURCE_TONE:
            return "Starting tone playback";
        case APP_MUSIC_SOURCE_SD_WAV:
            return "Starting SD audio playback";
        case APP_MUSIC_SOURCE_USB_AUDIO:
            return "Starting USB audio playback";
        default:
            return "Starting audio playback";
    }
}

static bool app_music_service_usb_stream_ready_to_start(size_t *out_buffered_frames)
{
    app_music_usb_stream_status_t status = {0};

    app_music_usb_stream_get_status(&status);
    if (out_buffered_frames != NULL) {
        *out_buffered_frames = status.buffered_frames;
    }

    return status.active && status.buffered_frames >= APP_MUSIC_USB_START_THRESHOLD_FRAMES;
}

static void app_music_service_copy_snapshot(app_music_snapshot_t *out_snapshot)
{
    if (out_snapshot == NULL) {
        return;
    }

    memset(out_snapshot, 0, sizeof(*out_snapshot));
    if (s_music_mutex == NULL) {
        return;
    }

    xSemaphoreTake(s_music_mutex, portMAX_DELAY);
    *out_snapshot = s_snapshot;
    xSemaphoreGive(s_music_mutex);
}

void app_music_service_log_boot_diagnostics(void)
{
    app_music_service_diag_init_if_needed();
    s_music_diag.boot_count++;

    esp_reset_reason_t reason = esp_reset_reason();
    ESP_LOGI(
        TAG,
        "reset reason: %s (%d), diag boot_count=%lu mark_count=%lu last_stage=%s last_error=%s tone=%u state=%u volume=%u sample_rate=%lu",
        app_music_service_reset_reason_to_text(reason),
        (int)reason,
        (unsigned long)s_music_diag.boot_count,
        (unsigned long)s_music_diag.mark_count,
        app_music_service_diag_stage_to_text(s_music_diag.stage),
        esp_err_to_name((esp_err_t)s_music_diag.current_error),
        (unsigned)s_music_diag.tone,
        (unsigned)s_music_diag.state,
        (unsigned)s_music_diag.volume_percent,
        (unsigned long)s_music_diag.sample_rate_hz
    );

    app_music_service_diag_mark(APP_MUSIC_DIAG_STAGE_BOOT, ESP_OK);
}

static void app_music_service_task(void *arg)
{
    (void)arg;

    int16_t *sample_buffer = calloc(APP_MUSIC_BUFFER_FRAMES * 2, sizeof(int16_t));
    app_music_playback_source_t active_source = {0};
    bool output_active = false;
    uint32_t target_sample_rate_hz = BOARD_AUDIO_OUTPUT_SAMPLE_RATE;
    const app_music_playback_source_env_t source_env = {
        .ensure_sd_ready = app_music_service_mount_sd_action,
        .refresh_sd_listing = app_music_service_refresh_sd_listing_action,
        .status_cb = app_music_service_download_status_cb,
        .ctx = NULL,
    };

    if (sample_buffer == NULL) {
        ESP_LOGE(TAG, "failed to allocate tone buffer");
        vTaskDelete(NULL);
        return;
    }

    while (true) {
        app_music_snapshot_t snapshot;
        esp_err_t err;
        app_music_service_copy_snapshot(&snapshot);

        if (!snapshot.ready || snapshot.state != APP_MUSIC_STATE_PLAYING) {
            app_music_service_diag_mark(APP_MUSIC_DIAG_STAGE_IDLE, ESP_OK);
            app_music_playback_source_close(&active_source);
            if (output_active) {
                app_music_service_diag_mark(APP_MUSIC_DIAG_STAGE_STOP_OUTPUT, ESP_OK);
            }
            if (audio_output_stop() != ESP_OK) {
                ESP_LOGW(TAG, "failed to stop audio output cleanly");
            }
            output_active = false;
            vTaskDelay(pdMS_TO_TICKS(30));
            continue;
        }

        if (!app_music_playback_source_is_open(&active_source)) {
            err = app_music_playback_source_open(&active_source, &snapshot, &source_env);
            if (err != ESP_OK) {
                if (snapshot.source == APP_MUSIC_SOURCE_TONE) {
                    app_music_service_set_status_text_fmt("Tone setup failed: %s", esp_err_to_name(err));
                } else {
                    ESP_LOGE(TAG, "failed to open audio source %s: %s", snapshot.wav_path, esp_err_to_name(err));
                }
                app_music_service_diag_mark(APP_MUSIC_DIAG_STAGE_FILL_BUFFER, err);
                app_music_service_set_state(APP_MUSIC_STATE_STOPPED);
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }

            err = app_music_service_configure_output_for_source(&active_source, &target_sample_rate_hz);
            if (err != ESP_OK) {
                app_music_playback_source_close(&active_source);
                app_music_service_set_status_text_fmt("Audio rate failed: %s", esp_err_to_name(err));
                app_music_service_diag_mark(APP_MUSIC_DIAG_STAGE_FILL_BUFFER, err);
                app_music_service_set_state(APP_MUSIC_STATE_STOPPED);
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }
        }

        if (app_music_playback_source_is_tone(&active_source)) {
            err = app_music_playback_source_update(&active_source, &snapshot);
            if (err != ESP_OK) {
                app_music_service_set_status_text_fmt("Tone update failed: %s", esp_err_to_name(err));
                app_music_service_diag_mark(APP_MUSIC_DIAG_STAGE_FILL_BUFFER, err);
                app_music_service_set_state(APP_MUSIC_STATE_STOPPED);
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }
        }

        if (app_music_playback_source_is_usb(&active_source) && !output_active) {
            size_t buffered_frames = 0U;
            if (!app_music_service_usb_stream_ready_to_start(&buffered_frames)) {
                app_music_service_set_status_text_fmt(
                    "Waiting for USB audio (%u/%u frames)",
                    (unsigned)buffered_frames,
                    (unsigned)APP_MUSIC_USB_START_THRESHOLD_FRAMES
                );
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
        }

        app_music_service_diag_mark(APP_MUSIC_DIAG_STAGE_FILL_BUFFER, ESP_OK);
        {
            size_t file_frame_count = 0;
            bool file_finished = false;

            err = app_music_playback_source_read(
                &active_source,
                sample_buffer,
                APP_MUSIC_BUFFER_FRAMES,
                &file_frame_count,
                &file_finished,
                &source_env
            );
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "failed to read audio source frames: %s", esp_err_to_name(err));
                app_music_playback_source_close(&active_source);
                app_music_service_diag_mark(APP_MUSIC_DIAG_STAGE_FILL_BUFFER, err);
                app_music_service_set_state(APP_MUSIC_STATE_STOPPED);
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }

            if (app_music_playback_source_is_file(&active_source) && file_frame_count == 0U && file_finished) {
                app_music_playback_source_close(&active_source);
                app_music_service_set_status_text("File playback finished");
                app_music_service_set_state(APP_MUSIC_STATE_STOPPED);
                vTaskDelay(pdMS_TO_TICKS(30));
                continue;
            }
        }

        if (!output_active) {
            app_music_service_diag_mark(APP_MUSIC_DIAG_STAGE_START_OUTPUT, ESP_OK);
            err = audio_output_preload_silence(APP_MUSIC_BUFFER_FRAMES);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "failed to preload audio silence: %s", esp_err_to_name(err));
                app_music_service_diag_mark(APP_MUSIC_DIAG_STAGE_START_OUTPUT, err);
                app_music_service_set_state(APP_MUSIC_STATE_STOPPED);
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }
            err = audio_output_start();
            if (err != ESP_OK) {
                app_music_service_set_status_text_fmt("Audio start failed: %s", esp_err_to_name(err));
                ESP_LOGE(TAG, "failed to start audio output: %s", esp_err_to_name(err));
                app_music_service_diag_mark(APP_MUSIC_DIAG_STAGE_START_OUTPUT, err);
                app_music_service_set_state(APP_MUSIC_STATE_STOPPED);
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }
            output_active = true;
            app_music_service_set_status_text(app_music_playback_source_playing_status_text(snapshot.source));
            s_music_diag.successful_writes++;
            s_render_loop_count++;
            if (s_render_loop_count <= 4 || (s_render_loop_count % 32U) == 0U) {
                ESP_LOGD(
                    TAG,
                    "render loop=%lu state=%s source=%s tone=%s volume=%u primed",
                    (unsigned long)s_render_loop_count,
                    app_music_service_state_to_text(snapshot.state),
                    app_music_service_source_to_text(snapshot.source),
                    app_music_service_tone_to_text(snapshot.tone),
                    snapshot.volume_percent
                );
            }
            continue;
        }
        app_music_service_diag_mark(APP_MUSIC_DIAG_STAGE_WRITE_BUFFER, ESP_OK);
        s_render_loop_count++;
        if (s_render_loop_count <= 4 || (s_render_loop_count % 32U) == 0U) {
            ESP_LOGD(
                TAG,
                "render loop=%lu state=%s source=%s tone=%s volume=%u stack_hw=%lu",
                (unsigned long)s_render_loop_count,
                app_music_service_state_to_text(snapshot.state),
                app_music_service_source_to_text(snapshot.source),
                app_music_service_tone_to_text(snapshot.tone),
                snapshot.volume_percent,
                (unsigned long)uxTaskGetStackHighWaterMark(NULL)
            );
        }
        err = audio_output_write_stereo(sample_buffer, APP_MUSIC_BUFFER_FRAMES, APP_MUSIC_TIMEOUT_MS);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "audio write failed: %s", esp_err_to_name(err));
            app_music_playback_source_close(&active_source);
            app_music_service_set_status_text_fmt("Audio write failed: %s", esp_err_to_name(err));
            app_music_service_diag_mark(APP_MUSIC_DIAG_STAGE_WRITE_BUFFER, err);
            app_music_service_set_state(APP_MUSIC_STATE_STOPPED);
            output_active = false;
            vTaskDelay(pdMS_TO_TICKS(100));
        } else {
            s_music_diag.successful_writes++;
        }
    }

    free(sample_buffer);
    vTaskDelete(NULL);
}

esp_err_t app_music_service_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "music service already initialized");
        return ESP_OK;
    }

    app_music_service_diag_mark(APP_MUSIC_DIAG_STAGE_INIT_SERVICE, ESP_OK);
    app_music_service_diag_mark(APP_MUSIC_DIAG_STAGE_INIT_AUDIO_OUTPUT, ESP_OK);
    ESP_RETURN_ON_ERROR(audio_output_init(), TAG, "failed to init audio output");

    s_music_mutex = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_music_mutex != NULL, ESP_ERR_NO_MEM, TAG, "failed to create music mutex");

    memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_snapshot.ready = true;
    s_snapshot.state = APP_MUSIC_STATE_STOPPED;
    s_snapshot.source = APP_MUSIC_SOURCE_TONE;
    s_snapshot.tone = APP_MUSIC_TONE_A4;
    s_snapshot.volume_percent = APP_MUSIC_VOLUME_PERCENT;
    s_snapshot.sample_rate_hz = audio_output_get_sample_rate();
    strncpy(s_snapshot.wav_path, APP_MUSIC_DEFAULT_WAV_PATH, sizeof(s_snapshot.wav_path) - 1U);
    strncpy(s_snapshot.status_text, "Tone ready", sizeof(s_snapshot.status_text) - 1U);

    app_music_service_diag_mark(APP_MUSIC_DIAG_STAGE_CREATE_TASK, ESP_OK);
    BaseType_t task_ok = xTaskCreatePinnedToCore(
        app_music_service_task,
        "app_music_task",
        1024 * 24,
        NULL,
        4,
        NULL,
        0
    );
    ESP_RETURN_ON_FALSE(task_ok == pdPASS, ESP_ERR_NO_MEM, TAG, "failed to create music task");

    s_initialized = true;
    s_render_loop_count = 0;
    ESP_LOGI(TAG, "music service initialized at %lu Hz", (unsigned long)s_snapshot.sample_rate_hz);
    return ESP_OK;
}

esp_err_t app_music_service_play(void)
{
    ESP_RETURN_ON_FALSE(s_music_mutex != NULL, ESP_ERR_INVALID_STATE, TAG, "music service not initialized");

    app_music_service_diag_mark(APP_MUSIC_DIAG_STAGE_PLAY_REQUEST, ESP_OK);
    xSemaphoreTake(s_music_mutex, portMAX_DELAY);
    s_snapshot.state = APP_MUSIC_STATE_PLAYING;
    app_music_service_reset_playback_counters(APP_MUSIC_DIAG_STAGE_PLAY_REQUEST);
    xSemaphoreGive(s_music_mutex);
    app_music_service_set_status_text(app_music_service_starting_status_text(s_snapshot.source));
    ESP_LOGI(TAG, "play request accepted");
    return ESP_OK;
}

esp_err_t app_music_service_stop(void)
{
    ESP_RETURN_ON_FALSE(s_music_mutex != NULL, ESP_ERR_INVALID_STATE, TAG, "music service not initialized");

    app_music_service_diag_mark(APP_MUSIC_DIAG_STAGE_STOP_REQUEST, ESP_OK);
    xSemaphoreTake(s_music_mutex, portMAX_DELAY);
    s_snapshot.state = APP_MUSIC_STATE_STOPPED;
    xSemaphoreGive(s_music_mutex);
    app_music_service_set_status_text("Playback stopped");
    ESP_LOGI(TAG, "stop request accepted");
    return ESP_OK;
}

esp_err_t app_music_service_toggle_playback(void)
{
    ESP_RETURN_ON_FALSE(s_music_mutex != NULL, ESP_ERR_INVALID_STATE, TAG, "music service not initialized");

    app_music_source_t source;
    const char *status_text;

    app_music_service_diag_mark(APP_MUSIC_DIAG_STAGE_TOGGLE_REQUEST, ESP_OK);
    xSemaphoreTake(s_music_mutex, portMAX_DELAY);
    s_snapshot.state = (s_snapshot.state == APP_MUSIC_STATE_PLAYING) ? APP_MUSIC_STATE_STOPPED : APP_MUSIC_STATE_PLAYING;
    app_music_state_t new_state = s_snapshot.state;
    source = s_snapshot.source;
    if (new_state == APP_MUSIC_STATE_PLAYING) {
        app_music_service_reset_playback_counters(APP_MUSIC_DIAG_STAGE_TOGGLE_REQUEST);
        status_text = app_music_service_starting_status_text(source);
    } else {
        status_text = "Playback stopped";
    }
    xSemaphoreGive(s_music_mutex);
    app_music_service_set_status_text(status_text);
    ESP_LOGI(TAG, "toggle request -> %s", app_music_service_state_to_text(new_state));
    return ESP_OK;
}

esp_err_t app_music_service_set_source(app_music_source_t source)
{
    ESP_RETURN_ON_FALSE(app_music_service_source_is_valid(source), ESP_ERR_INVALID_ARG, TAG, "invalid source");
    ESP_RETURN_ON_FALSE(s_music_mutex != NULL, ESP_ERR_INVALID_STATE, TAG, "music service not initialized");

    xSemaphoreTake(s_music_mutex, portMAX_DELAY);
    s_snapshot.source = source;
    if (s_snapshot.state == APP_MUSIC_STATE_PLAYING) {
        s_snapshot.state = APP_MUSIC_STATE_STOPPED;
    }
    s_snapshot.sample_rate_hz = app_music_service_default_sample_rate_for_source(source);
    xSemaphoreGive(s_music_mutex);

    if (source == APP_MUSIC_SOURCE_TONE) {
        app_music_service_set_status_text("Tone mode selected");
    } else if (source == APP_MUSIC_SOURCE_USB_AUDIO) {
        app_music_service_set_status_text("USB audio mode selected");
    } else {
        ESP_RETURN_ON_ERROR(app_music_service_refresh_wav_files(), TAG, "failed to refresh wav files");
    }

    ESP_LOGI(TAG, "music source changed to %s", app_music_service_source_to_text(source));
    return ESP_OK;
}

esp_err_t app_music_service_set_tone(app_music_tone_t tone)
{
    ESP_RETURN_ON_FALSE(app_music_service_tone_is_valid(tone), ESP_ERR_INVALID_ARG, TAG, "invalid tone");
    ESP_RETURN_ON_FALSE(s_music_mutex != NULL, ESP_ERR_INVALID_STATE, TAG, "music service not initialized");

    app_music_service_diag_mark(APP_MUSIC_DIAG_STAGE_TONE_CHANGE, ESP_OK);
    xSemaphoreTake(s_music_mutex, portMAX_DELAY);
    s_snapshot.tone = tone;
    xSemaphoreGive(s_music_mutex);
    app_music_service_set_status_text("Tone selection updated");
    ESP_LOGI(TAG, "tone changed to %s", app_music_service_tone_to_text(tone));
    return ESP_OK;
}

esp_err_t app_music_service_set_wav_path(const char *path)
{
    ESP_RETURN_ON_FALSE(path != NULL, ESP_ERR_INVALID_ARG, TAG, "path is null");
    ESP_RETURN_ON_FALSE(s_music_mutex != NULL, ESP_ERR_INVALID_STATE, TAG, "music service not initialized");

    xSemaphoreTake(s_music_mutex, portMAX_DELAY);
    strncpy(s_snapshot.wav_path, path, sizeof(s_snapshot.wav_path) - 1U);
    s_snapshot.wav_path[sizeof(s_snapshot.wav_path) - 1U] = '\0';
    xSemaphoreGive(s_music_mutex);
    app_music_service_set_status_text("Audio path updated");
    ESP_LOGI(TAG, "audio path set to %s", path);
    return ESP_OK;
}

esp_err_t app_music_service_select_wav_file(uint8_t index)
{
    ESP_RETURN_ON_FALSE(s_music_mutex != NULL, ESP_ERR_INVALID_STATE, TAG, "music service not initialized");

    xSemaphoreTake(s_music_mutex, portMAX_DELAY);
    if (index >= s_wav_file_count) {
        xSemaphoreGive(s_music_mutex);
        ESP_LOGE(TAG, "invalid file index: %u", (unsigned)index);
        return ESP_ERR_INVALID_ARG;
    }
    s_wav_selected_index = index;
    app_music_service_apply_selected_wav_locked();
    if (s_snapshot.state == APP_MUSIC_STATE_PLAYING && s_snapshot.source == APP_MUSIC_SOURCE_SD_WAV) {
        s_snapshot.state = APP_MUSIC_STATE_STOPPED;
    }
    xSemaphoreGive(s_music_mutex);
    app_music_service_set_status_text("File selection updated");
    ESP_LOGI(TAG, "file selection changed to %s", app_music_service_basename(s_snapshot.wav_path));
    return ESP_OK;
}

void app_music_service_get_wav_dropdown_options(
    char *buffer,
    size_t buffer_size,
    uint8_t *out_selected_index,
    uint8_t *out_count
)
{
    size_t offset = 0;

    if (buffer == NULL || buffer_size == 0U || s_music_mutex == NULL) {
        return;
    }

    xSemaphoreTake(s_music_mutex, portMAX_DELAY);
    if (out_selected_index != NULL) {
        *out_selected_index = s_wav_selected_index;
    }
    if (out_count != NULL) {
        *out_count = s_wav_file_count;
    }

    buffer[0] = '\0';
    if (s_wav_file_count == 0U) {
        strncpy(buffer, "No audio found", buffer_size - 1U);
        buffer[buffer_size - 1U] = '\0';
        xSemaphoreGive(s_music_mutex);
        return;
    }

    for (uint8_t i = 0; i < s_wav_file_count; i++) {
        const char *label = app_music_service_basename(s_wav_paths[i]);
        int written = snprintf(buffer + offset, buffer_size - offset, "%s%s", i == 0U ? "" : "\n", label);
        if (written < 0 || (size_t)written >= (buffer_size - offset)) {
            break;
        }
        offset += (size_t)written;
    }
    xSemaphoreGive(s_music_mutex);
}

void app_music_service_get_snapshot(app_music_snapshot_t *out_snapshot)
{
    app_music_service_copy_snapshot(out_snapshot);
}

void app_music_service_get_diagnostics(app_music_diag_info_t *out_diag)
{
    if (out_diag == NULL) {
        return;
    }

    app_music_service_diag_init_if_needed();
    memset(out_diag, 0, sizeof(*out_diag));
    out_diag->boot_count = s_music_diag.boot_count;
    out_diag->mark_count = s_music_diag.mark_count;
    out_diag->successful_writes = s_music_diag.successful_writes;
    out_diag->current_error = (esp_err_t)s_music_diag.current_error;
    out_diag->last_failure_error = (esp_err_t)s_music_diag.last_failure_error;
    out_diag->current_stage = s_music_diag.stage;
    out_diag->last_failure_stage = s_music_diag.last_failure_stage;
}

esp_err_t app_music_service_usb_begin(uint32_t sample_rate_hz)
{
    ESP_RETURN_ON_ERROR(app_music_usb_stream_start(sample_rate_hz), TAG, "failed to start usb audio stream");

    if (s_music_mutex != NULL) {
        xSemaphoreTake(s_music_mutex, portMAX_DELAY);
        if (s_snapshot.source == APP_MUSIC_SOURCE_USB_AUDIO) {
            s_snapshot.sample_rate_hz = sample_rate_hz;
        }
        xSemaphoreGive(s_music_mutex);
    }

    return ESP_OK;
}

esp_err_t app_music_service_usb_push_stereo(
    const int16_t *samples,
    size_t frame_count,
    size_t *out_accepted_frames
)
{
    return app_music_usb_stream_push_stereo(samples, frame_count, out_accepted_frames);
}

void app_music_service_usb_end(void)
{
    app_music_usb_stream_stop();
}

void app_music_service_usb_get_status(app_music_usb_audio_status_t *out_status)
{
    app_music_usb_stream_status_t usb_status = {0};

    if (out_status == NULL) {
        return;
    }

    app_music_usb_stream_get_status(&usb_status);
    out_status->active = usb_status.active;
    out_status->sample_rate_hz = usb_status.sample_rate_hz;
    out_status->buffered_frames = usb_status.buffered_frames;
    out_status->capacity_frames = usb_status.capacity_frames;
    out_status->underrun_count = usb_status.underrun_count;
    out_status->overflow_count = usb_status.overflow_count;
}

const char *app_music_service_state_to_text(app_music_state_t state)
{
    switch (state) {
        case APP_MUSIC_STATE_STOPPED:
            return "Stopped";
        case APP_MUSIC_STATE_PLAYING:
            return "Playing";
        default:
            return "Unknown";
    }
}

const char *app_music_service_source_to_text(app_music_source_t source)
{
    switch (source) {
        case APP_MUSIC_SOURCE_TONE:
            return "Tone";
        case APP_MUSIC_SOURCE_SD_WAV:
            return "SD Audio";
        case APP_MUSIC_SOURCE_USB_AUDIO:
            return "USB Audio";
        default:
            return "Unknown";
    }
}

const char *app_music_service_tone_to_text(app_music_tone_t tone)
{
    if (!app_music_service_tone_is_valid(tone)) {
        return "Unknown";
    }

    return TONE_TABLE[tone].name;
}
