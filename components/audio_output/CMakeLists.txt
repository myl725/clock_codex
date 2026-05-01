idf_component_register(
    SRCS "audio_output.c"
    INCLUDE_DIRS "include"
    REQUIRES bsp esp_driver_i2s freertos
)
