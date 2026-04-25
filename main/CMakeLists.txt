idf_component_register(
    SRCS "led_control.c" "hello_world.c" 
    INCLUDE_DIRS "."
    REQUIRES driver esp_driver_i2s esp-sr esp_driver_ledc
)