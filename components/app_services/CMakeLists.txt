idf_component_register(
    SRCS "app_speech_service.c"
    INCLUDE_DIRS "include"
    REQUIRES audio_input freertos esp_timer
)
