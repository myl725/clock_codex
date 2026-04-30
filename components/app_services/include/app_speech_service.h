#ifndef APP_SPEECH_SERVICE_H
#define APP_SPEECH_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
    APP_SPEECH_STATE_BOOTING = 0,
    APP_SPEECH_STATE_LISTENING,
    APP_SPEECH_STATE_AWAKE,
    APP_SPEECH_STATE_RECOGNIZED,
    APP_SPEECH_STATE_TIMEOUT,
    APP_SPEECH_STATE_ERROR,
} app_speech_state_t;

typedef struct {
    bool ready;
    app_speech_state_t state;
    uint8_t level_percent;
    float probability;
    int command_id;
    char wake_hint[32];
    char last_text[64];
    char detail[96];
} app_speech_snapshot_t;

esp_err_t app_speech_service_init(void);
void app_speech_service_get_snapshot(app_speech_snapshot_t *out_snapshot);

#endif
