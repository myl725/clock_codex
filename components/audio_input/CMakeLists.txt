idf_component_register(
    SRCS "audio_input.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "${IDF_PATH}/components/driver/deprecated/driver"
    REQUIRES driver esp_driver_gpio esp_driver_spi esp_driver_i2s bsp
)
