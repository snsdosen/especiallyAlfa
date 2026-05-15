idf_component_register(SRCS "textcom.c"
                    INCLUDE_DIRS "include"
                    REQUIRES driver esp_driver_pcnt esp_ringbuf esp_driver_spi esp_driver_gpio esp_timer)