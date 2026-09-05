idf_component_register(SRCS "moisture.c" "pump.c" "main.c" "mqtt.c" "wifi.c"
                    PRIV_REQUIRES esp_wifi nvs_flash esp_timer esp_hw_support
                    REQUIRES driver
                    REQUIRES esp_adc
                    REQUIRES esp_driver_gpio
                    INCLUDE_DIRS ".")
