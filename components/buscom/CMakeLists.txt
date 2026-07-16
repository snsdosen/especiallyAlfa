idf_component_register(SRCS "buscom.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_driver_uart esp_driver_gpio esp_ringbuf esp_hw_support driver esp_timer esp_pm)