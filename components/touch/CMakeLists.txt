idf_component_register(
    SRCS "touch_xpt2046.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_spi display bsp
)
