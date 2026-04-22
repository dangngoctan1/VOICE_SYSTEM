idf_component_register(
    SRCS "hello_world.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_driver_i2s esp-sr
)