idf_component_register(
    SRCS "display_ili9341.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_spi esp_driver_gpio bsp
)
