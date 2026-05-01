idf_component_register(
    SRCS "app_led_service.c" "app_music_service.c" "app_music_wav.c" "app_music_mp3.c" "app_music_download.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES freertos esp_timer ws2812 audio_output fatfs sdmmc esp_driver_sdspi esp_driver_spi esp_wifi esp_event esp_netif esp_http_client esp-tls mbedtls
)
