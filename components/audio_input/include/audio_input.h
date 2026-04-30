#ifndef AUDIO_INPUT_H
#define AUDIO_INPUT_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t audio_input_init(void);
esp_err_t audio_input_read(int16_t *buffer, size_t sample_count, size_t *samples_read);
uint32_t audio_input_get_sample_rate(void);
int audio_input_get_feed_channel_count(void);
const char *audio_input_get_input_format(void);

#endif
