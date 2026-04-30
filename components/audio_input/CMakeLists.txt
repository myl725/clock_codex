idf_component_register(
    SRCS "audio_input.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio esp_driver_spi esp_driver_i2s bsp
)
