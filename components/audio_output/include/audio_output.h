#ifndef AUDIO_OUTPUT_H
#define AUDIO_OUTPUT_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t audio_output_init(void);
esp_err_t audio_output_preload_stereo(const int16_t *samples, size_t frame_count);
esp_err_t audio_output_preload_silence(size_t frame_count);
esp_err_t audio_output_start(void);
esp_err_t audio_output_stop(void);
esp_err_t audio_output_write_stereo(const int16_t *samples, size_t frame_count, uint32_t timeout_ms);
esp_err_t audio_output_write_silence(size_t frame_count, uint32_t timeout_ms);
esp_err_t audio_output_set_sample_rate(uint32_t sample_rate_hz);
uint32_t audio_output_get_sample_rate(void);

#endif
