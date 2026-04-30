#include "app_speech_service.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio_input.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_mn_iface.h"
#include "esp_mn_models.h"
#include "esp_mn_speech_commands.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "model_path.h"

typedef struct {
    int command_id;
    const char *phrase_pinyin;
    const char *display_text;
} speech_phrase_t;

static const char *TAG = "app_speech";
static const char *WAKE_HINT_TEXT = "嗨乐鑫";
static const speech_phrase_t SPEECH_PHRASES[] = {
    {1, "ni hao xiao zhong", "你好，小钟"},
    {2, "xian shi shi jian", "显示时间"},
    {3, "jin ru ce shi", "进入测试"},
    {4, "jie shu ce shi", "结束测试"},
};

static const esp_afe_sr_iface_t *s_afe_handle;
static esp_afe_sr_data_t *s_afe_data;
static srmodel_list_t *s_models;
static const esp_mn_iface_t *s_multinet;
static model_iface_data_t *s_multinet_data;
static SemaphoreHandle_t s_snapshot_mutex;
static app_speech_snapshot_t s_snapshot;
static volatile bool s_service_running;
static bool s_service_initialized;

static const char *app_speech_phrase_to_text(int command_id)
{
    for (size_t i = 0; i < (sizeof(SPEECH_PHRASES) / sizeof(SPEECH_PHRASES[0])); i++) {
        if (SPEECH_PHRASES[i].command_id == command_id) {
            return SPEECH_PHRASES[i].display_text;
        }
    }

    return "未映射命令";
}

static void app_speech_set_snapshot_locked(
    app_speech_state_t state,
    const char *last_text,
    const char *detail,
    float probability,
    int command_id
)
{
    s_snapshot.state = state;
    s_snapshot.probability = probability;
    s_snapshot.command_id = command_id;

    if (last_text != NULL) {
        strlcpy(s_snapshot.last_text, last_text, sizeof(s_snapshot.last_text));
    }
    if (detail != NULL) {
        strlcpy(s_snapshot.detail, detail, sizeof(s_snapshot.detail));
    }
}

static void app_speech_update_snapshot(
    app_speech_state_t state,
    const char *last_text,
    const char *detail,
    float probability,
    int command_id
)
{
    if (s_snapshot_mutex == NULL) {
        return;
    }

    xSemaphoreTake(s_snapshot_mutex, portMAX_DELAY);
    app_speech_set_snapshot_locked(state, last_text, detail, probability, command_id);
    xSemaphoreGive(s_snapshot_mutex);
}

static void app_speech_set_level(uint8_t level_percent)
{
    if (s_snapshot_mutex == NULL) {
        return;
    }

    xSemaphoreTake(s_snapshot_mutex, portMAX_DELAY);
    s_snapshot.level_percent = level_percent;
    xSemaphoreGive(s_snapshot_mutex);
}

static esp_err_t app_speech_configure_commands(void)
{
    ESP_RETURN_ON_FALSE(s_multinet != NULL && s_multinet_data != NULL, ESP_ERR_INVALID_STATE, TAG, "multinet not ready");

    ESP_RETURN_ON_ERROR(esp_mn_commands_alloc(s_multinet, s_multinet_data), TAG, "failed to allocate speech commands");
    ESP_RETURN_ON_ERROR(esp_mn_commands_clear(), TAG, "failed to clear speech commands");

    for (size_t i = 0; i < (sizeof(SPEECH_PHRASES) / sizeof(SPEECH_PHRASES[0])); i++) {
        ESP_RETURN_ON_ERROR(
            esp_mn_commands_add(SPEECH_PHRASES[i].command_id, SPEECH_PHRASES[i].phrase_pinyin),
            TAG,
            "failed to add phrase: %s",
            SPEECH_PHRASES[i].phrase_pinyin
        );
    }

    esp_mn_error_t *errors = esp_mn_commands_update();
    ESP_RETURN_ON_FALSE(errors == NULL, ESP_ERR_INVALID_STATE, TAG, "failed to update multinet speech commands");
    s_multinet->print_active_speech_commands(s_multinet_data);

    return ESP_OK;
}

static void app_speech_feed_task(void *arg)
{
    (void)arg;

    const int feed_chunksize = s_afe_handle->get_feed_chunksize(s_afe_data);
    const int feed_channels = s_afe_handle->get_feed_channel_num(s_afe_data);
    const size_t feed_sample_count = (size_t)feed_chunksize * feed_channels;
    int16_t *feed_buffer = calloc(feed_sample_count, sizeof(int16_t));

    if (feed_buffer == NULL) {
        app_speech_update_snapshot(APP_SPEECH_STATE_ERROR, "内存不足", "feed buffer alloc failed", 0.0f, -1);
        vTaskDelete(NULL);
        return;
    }

    while (s_service_running) {
        size_t samples_read = 0;
        esp_err_t err = audio_input_read(feed_buffer, feed_sample_count, &samples_read);
        if (err != ESP_OK || samples_read != feed_sample_count) {
            app_speech_update_snapshot(APP_SPEECH_STATE_ERROR, "麦克风读取失败", esp_err_to_name(err), 0.0f, -1);
            break;
        }

        int32_t peak = 0;
        for (size_t i = 0; i < feed_sample_count; i += 2) {
            int32_t sample = feed_buffer[i];
            if (sample < 0) {
                sample = -sample;
            }
            if (sample > peak) {
                peak = sample;
            }
        }

        uint8_t level_percent = (uint8_t)((peak * 100) / 2048);
        if (level_percent > 100) {
            level_percent = 100;
        }
        app_speech_set_level(level_percent);

        s_afe_handle->feed(s_afe_data, feed_buffer);
    }

    free(feed_buffer);
    vTaskDelete(NULL);
}

static void app_speech_detect_task(void *arg)
{
    (void)arg;

    bool wakeup_active = false;
    app_speech_update_snapshot(APP_SPEECH_STATE_LISTENING, "等待唤醒", "先说：嗨乐鑫", 0.0f, -1);

    while (s_service_running) {
        afe_fetch_result_t *result = s_afe_handle->fetch(s_afe_data);
        if (result == NULL || result->ret_value == ESP_FAIL) {
            app_speech_update_snapshot(APP_SPEECH_STATE_ERROR, "AFE fetch 失败", "请检查模型和麦克风连线", 0.0f, -1);
            break;
        }

        if (result->wakeup_state == WAKENET_DETECTED) {
            wakeup_active = true;
            s_multinet->clean(s_multinet_data);
            app_speech_update_snapshot(APP_SPEECH_STATE_AWAKE, "唤醒成功", "请说命令词", 0.0f, -1);
        }

        if (!wakeup_active) {
            continue;
        }

        esp_mn_state_t mn_state = s_multinet->detect(s_multinet_data, result->data);
        if (mn_state == ESP_MN_STATE_DETECTING) {
            continue;
        }

        if (mn_state == ESP_MN_STATE_DETECTED) {
            esp_mn_results_t *mn_result = s_multinet->get_results(s_multinet_data);
            if (mn_result != NULL && mn_result->num > 0) {
                const char *text = app_speech_phrase_to_text(mn_result->command_id[0]);
                app_speech_update_snapshot(
                    APP_SPEECH_STATE_RECOGNIZED,
                    text,
                    mn_result->string,
                    mn_result->prob[0],
                    mn_result->command_id[0]
                );
                ESP_LOGI(
                    TAG,
                    "recognized command_id=%d text=%s raw=%s prob=%.3f",
                    mn_result->command_id[0],
                    text,
                    mn_result->string,
                    mn_result->prob[0]
                );
            }
            continue;
        }

        if (mn_state == ESP_MN_STATE_TIMEOUT) {
            s_afe_handle->enable_wakenet(s_afe_data);
            wakeup_active = false;
            app_speech_update_snapshot(APP_SPEECH_STATE_TIMEOUT, "命令超时", "请重新说：嗨乐鑫", 0.0f, -1);
        }
    }

    vTaskDelete(NULL);
}

esp_err_t app_speech_service_init(void)
{
    if (s_service_initialized) {
        ESP_LOGW(TAG, "speech service already initialized");
        return ESP_OK;
    }

    s_snapshot_mutex = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_snapshot_mutex != NULL, ESP_ERR_NO_MEM, TAG, "failed to create snapshot mutex");

    memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_snapshot.ready = false;
    s_snapshot.command_id = -1;
    strlcpy(s_snapshot.wake_hint, WAKE_HINT_TEXT, sizeof(s_snapshot.wake_hint));
    strlcpy(s_snapshot.last_text, "语音服务启动中", sizeof(s_snapshot.last_text));
    strlcpy(s_snapshot.detail, "准备初始化 INMP441", sizeof(s_snapshot.detail));

    ESP_RETURN_ON_ERROR(audio_input_init(), TAG, "failed to init INMP441 input");

    s_models = esp_srmodel_init("model");
    ESP_RETURN_ON_FALSE(s_models != NULL, ESP_ERR_NOT_FOUND, TAG, "failed to load speech models from model partition");

    afe_config_t *afe_config = afe_config_init(audio_input_get_input_format(), s_models, AFE_TYPE_SR, AFE_MODE_LOW_COST);
    ESP_RETURN_ON_FALSE(afe_config != NULL, ESP_ERR_NO_MEM, TAG, "failed to init AFE config");

    s_afe_handle = esp_afe_handle_from_config(afe_config);
    ESP_RETURN_ON_FALSE(s_afe_handle != NULL, ESP_ERR_INVALID_STATE, TAG, "failed to get AFE handle");

    s_afe_data = s_afe_handle->create_from_config(afe_config);
    afe_config_free(afe_config);
    ESP_RETURN_ON_FALSE(s_afe_data != NULL, ESP_ERR_INVALID_STATE, TAG, "failed to create AFE instance");

    char *mn_name = esp_srmodel_filter(s_models, ESP_MN_PREFIX, ESP_MN_CHINESE);
    ESP_RETURN_ON_FALSE(mn_name != NULL, ESP_ERR_NOT_FOUND, TAG, "failed to find Chinese multinet model");

    s_multinet = esp_mn_handle_from_name(mn_name);
    ESP_RETURN_ON_FALSE(s_multinet != NULL, ESP_ERR_NOT_FOUND, TAG, "failed to get multinet interface");

    s_multinet_data = s_multinet->create(mn_name, 6000);
    ESP_RETURN_ON_FALSE(s_multinet_data != NULL, ESP_ERR_INVALID_STATE, TAG, "failed to create multinet model");
    ESP_RETURN_ON_FALSE(
        s_multinet->get_samp_chunksize(s_multinet_data) == s_afe_handle->get_fetch_chunksize(s_afe_data),
        ESP_ERR_INVALID_STATE,
        TAG,
        "multinet chunk size does not match AFE fetch size"
    );
    ESP_RETURN_ON_FALSE(
        s_afe_handle->get_feed_channel_num(s_afe_data) == audio_input_get_feed_channel_count(),
        ESP_ERR_INVALID_STATE,
        TAG,
        "AFE feed channel count does not match audio input format"
    );

    ESP_RETURN_ON_ERROR(app_speech_configure_commands(), TAG, "failed to configure commands");

    s_service_running = true;
    xTaskCreatePinnedToCore(app_speech_feed_task, "speech_feed", 1024 * 10, NULL, 4, NULL, 0);
    xTaskCreatePinnedToCore(app_speech_detect_task, "speech_detect", 1024 * 10, NULL, 4, NULL, 1);

    xSemaphoreTake(s_snapshot_mutex, portMAX_DELAY);
    s_snapshot.ready = true;
    app_speech_set_snapshot_locked(APP_SPEECH_STATE_LISTENING, "等待唤醒", "先说：嗨乐鑫", 0.0f, -1);
    xSemaphoreGive(s_snapshot_mutex);

    s_service_initialized = true;
    ESP_LOGI(TAG, "speech service initialized with wake word: %s", WAKE_HINT_TEXT);
    return ESP_OK;
}

void app_speech_service_get_snapshot(app_speech_snapshot_t *out_snapshot)
{
    if (out_snapshot == NULL) {
        return;
    }

    memset(out_snapshot, 0, sizeof(*out_snapshot));
    out_snapshot->command_id = -1;

    if (s_snapshot_mutex == NULL) {
        strlcpy(out_snapshot->wake_hint, WAKE_HINT_TEXT, sizeof(out_snapshot->wake_hint));
        return;
    }

    xSemaphoreTake(s_snapshot_mutex, portMAX_DELAY);
    *out_snapshot = s_snapshot;
    xSemaphoreGive(s_snapshot_mutex);
}
