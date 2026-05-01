#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#if __has_include("app_local_secrets.h")
#include "app_local_secrets.h"
#endif

#define APP_MUSIC_SD_MOUNT_POINT "/sdcard"
#define APP_MUSIC_DEFAULT_WAV_PATH "/sdcard/music.wav"
#define APP_MUSIC_DEFAULT_WAV_URL "https://raw.githubusercontent.com/espressif/esp-dsp/master/applications/lyrat_board_app/spiffs/16bit_mono_44_1_khz.wav"

#ifndef APP_WIFI_SSID
#define APP_WIFI_SSID ""
#endif

#ifndef APP_WIFI_PASSWORD
#define APP_WIFI_PASSWORD ""
#endif

#endif
