idf_component_register(
    SRCS "ws2812_driver.c"
    INCLUDE_DIRS "include"
    REQUIRES bsp esp_driver_rmt
)
