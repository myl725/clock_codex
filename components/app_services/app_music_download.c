#include "app_music_download.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define APP_MUSIC_WIFI_CONNECTED_BIT BIT0
#define APP_MUSIC_WIFI_FAILED_BIT BIT1
#define APP_MUSIC_WIFI_MAX_RETRIES 8
#define APP_MUSIC_WIFI_TIMEOUT_MS 20000
#define APP_MUSIC_HTTP_BUFFER_SIZE 2048
#define APP_MUSIC_WAV_MIN_SIZE_BYTES 44

static const char *TAG = "app_music_download";

static EventGroupHandle_t s_wifi_event_group;
static esp_event_handler_instance_t s_wifi_any_id;
static esp_event_handler_instance_t s_ip_got_ip;
static bool s_wifi_initialized;
static bool s_wifi_connected;
static int s_wifi_retry_count;

static void app_music_download_report_status(
    app_music_download_status_cb_t status_cb,
    void *status_ctx,
    const char *status_text
)
{
    if (status_cb != NULL && status_text != NULL) {
        status_cb(status_text, status_ctx);
    }
}

static void app_music_download_wifi_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data
)
{
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_wifi_connected = false;
        if (s_wifi_retry_count < APP_MUSIC_WIFI_MAX_RETRIES) {
            s_wifi_retry_count++;
            esp_wifi_connect();
        } else if (s_wifi_event_group != NULL) {
            xEventGroupSetBits(s_wifi_event_group, APP_MUSIC_WIFI_FAILED_BIT);
        }
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_wifi_retry_count = 0;
        s_wifi_connected = true;
        if (s_wifi_event_group != NULL) {
            xEventGroupSetBits(s_wifi_event_group, APP_MUSIC_WIFI_CONNECTED_BIT);
        }
    }
}

static esp_err_t app_music_download_init_wifi_once(void)
{
    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    wifi_config_t wifi_cfg = {0};

    if (s_wifi_initialized) {
        return ESP_OK;
    }

    ESP_RETURN_ON_FALSE(strlen(APP_WIFI_SSID) > 0U, ESP_ERR_INVALID_STATE, TAG, "wifi ssid is empty");

    s_wifi_event_group = xEventGroupCreate();
    ESP_RETURN_ON_FALSE(s_wifi_event_group != NULL, ESP_ERR_NO_MEM, TAG, "failed to create wifi event group");

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "failed to init esp netif");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "failed to create event loop");
    esp_netif_create_default_wifi_sta();

    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_cfg), TAG, "failed to init wifi");
    ESP_RETURN_ON_ERROR(
        esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &app_music_download_wifi_event_handler, NULL, &s_wifi_any_id),
        TAG,
        "failed to register wifi handler"
    );
    ESP_RETURN_ON_ERROR(
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &app_music_download_wifi_event_handler, NULL, &s_ip_got_ip),
        TAG,
        "failed to register ip handler"
    );

    strncpy((char *)wifi_cfg.sta.ssid, APP_WIFI_SSID, sizeof(wifi_cfg.sta.ssid) - 1U);
    strncpy((char *)wifi_cfg.sta.password, APP_WIFI_PASSWORD, sizeof(wifi_cfg.sta.password) - 1U);
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_cfg.sta.pmf_cfg.capable = true;
    wifi_cfg.sta.pmf_cfg.required = false;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "failed to set wifi mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg), TAG, "failed to set wifi config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "failed to start wifi");

    s_wifi_initialized = true;
    ESP_LOGI(TAG, "wifi station initialized");
    return ESP_OK;
}

static esp_err_t app_music_download_ensure_wifi_connected(
    app_music_download_status_cb_t status_cb,
    void *status_ctx
)
{
    EventBits_t bits;

    if (s_wifi_connected) {
        app_music_download_report_status(status_cb, status_ctx, "Wi-Fi connected");
        return ESP_OK;
    }

    app_music_download_report_status(status_cb, status_ctx, "Initializing Wi-Fi");
    ESP_RETURN_ON_ERROR(app_music_download_init_wifi_once(), TAG, "failed to init wifi");

    xEventGroupClearBits(s_wifi_event_group, APP_MUSIC_WIFI_CONNECTED_BIT | APP_MUSIC_WIFI_FAILED_BIT);
    s_wifi_retry_count = 0;
    app_music_download_report_status(status_cb, status_ctx, "Connecting Wi-Fi");
    ESP_RETURN_ON_ERROR(esp_wifi_connect(), TAG, "failed to start wifi connect");

    bits = xEventGroupWaitBits(
        s_wifi_event_group,
        APP_MUSIC_WIFI_CONNECTED_BIT | APP_MUSIC_WIFI_FAILED_BIT,
        pdFALSE,
        pdFALSE,
        pdMS_TO_TICKS(APP_MUSIC_WIFI_TIMEOUT_MS)
    );

    if ((bits & APP_MUSIC_WIFI_CONNECTED_BIT) != 0U) {
        ESP_LOGI(TAG, "wifi connected");
        app_music_download_report_status(status_cb, status_ctx, "Wi-Fi connected");
        return ESP_OK;
    }
    if ((bits & APP_MUSIC_WIFI_FAILED_BIT) != 0U) {
        app_music_download_report_status(status_cb, status_ctx, "Wi-Fi connect failed");
        return ESP_FAIL;
    }
    app_music_download_report_status(status_cb, status_ctx, "Wi-Fi connect timeout");
    return ESP_ERR_TIMEOUT;
}

esp_err_t app_music_download_default_wav_to_sd(
    const char *destination_path,
    app_music_download_status_cb_t status_cb,
    void *status_ctx
)
{
    esp_http_client_config_t http_cfg = {
        .url = APP_MUSIC_DEFAULT_WAV_URL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
        .buffer_size = APP_MUSIC_HTTP_BUFFER_SIZE,
    };
    esp_http_client_handle_t client = NULL;
    FILE *handle = NULL;
    uint8_t *buffer = NULL;
    int64_t content_length = -1;
    int http_status = 0;
    size_t total_bytes_written = 0;
    esp_err_t err;

    ESP_RETURN_ON_FALSE(destination_path != NULL, ESP_ERR_INVALID_ARG, TAG, "destination path is null");
    ESP_RETURN_ON_ERROR(app_music_download_ensure_wifi_connected(status_cb, status_ctx), TAG, "wifi not ready");

    app_music_download_report_status(status_cb, status_ctx, "Opening SD file for download");
    handle = fopen(destination_path, "wb");
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_STATE, TAG, "failed to open %s for writing", destination_path);

    client = esp_http_client_init(&http_cfg);
    if (client == NULL) {
        fclose(handle);
        return ESP_ERR_NO_MEM;
    }

    app_music_download_report_status(status_cb, status_ctx, "Starting HTTPS download");
    err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        fclose(handle);
        return err;
    }

    content_length = esp_http_client_fetch_headers(client);
    http_status = esp_http_client_get_status_code(client);
    if (http_status != 200) {
        app_music_download_report_status(status_cb, status_ctx, "HTTP status not 200");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        fclose(handle);
        remove(destination_path);
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (content_length > 0) {
        char status_text[96];
        snprintf(
            status_text,
            sizeof(status_text),
            "Downloading %lu bytes",
            (unsigned long)content_length
        );
        app_music_download_report_status(status_cb, status_ctx, status_text);
    }

    buffer = malloc(APP_MUSIC_HTTP_BUFFER_SIZE);
    if (buffer == NULL) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        fclose(handle);
        return ESP_ERR_NO_MEM;
    }

    while (true) {
        int bytes_read = esp_http_client_read(client, (char *)buffer, APP_MUSIC_HTTP_BUFFER_SIZE);
        if (bytes_read < 0) {
            err = ESP_FAIL;
            break;
        }
        if (bytes_read == 0) {
            err = ESP_OK;
            break;
        }
        if (fwrite(buffer, 1, (size_t)bytes_read, handle) != (size_t)bytes_read) {
            err = ESP_FAIL;
            break;
        }
        total_bytes_written += (size_t)bytes_read;
    }

    free(buffer);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    fclose(handle);

    if (err != ESP_OK) {
        remove(destination_path);
        return err;
    }

    if (total_bytes_written < APP_MUSIC_WAV_MIN_SIZE_BYTES) {
        app_music_download_report_status(status_cb, status_ctx, "Downloaded file too small");
        remove(destination_path);
        return ESP_ERR_INVALID_RESPONSE;
    }

    {
        char status_text[96];
        snprintf(
            status_text,
            sizeof(status_text),
            "Download complete (%lu bytes)",
            (unsigned long)total_bytes_written
        );
        app_music_download_report_status(status_cb, status_ctx, status_text);
    }
    ESP_LOGI(TAG, "downloaded wav to %s", destination_path);
    return ESP_OK;
}
