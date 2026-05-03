idf_component_register(
    SRCS "app_led_service.c" "app_music_service.c" "app_music_source.c" "app_music_playback_source.c" "app_music_usb_stream.c" "app_music_wav.c" "app_music_mp3.c" "app_music_download.c" "app_usb_audio_service.c" "app_usb_audio_descriptors.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES freertos esp_timer ws2812 audio_output fatfs sdmmc esp_driver_sdspi esp_driver_spi esp_wifi esp_event esp_netif esp_http_client esp-tls mbedtls esp_tinyusb espressif__esp_tinyusb
)

idf_build_get_property(build_components BUILD_COMPONENTS)
if(tinyusb IN_LIST build_components)
    set(tinyusb_name tinyusb)
else()
    set(tinyusb_name espressif__tinyusb)
endif()

idf_component_get_property(tusb_lib ${tinyusb_name} COMPONENT_LIB)
target_include_directories(${tusb_lib} BEFORE PRIVATE ".")
