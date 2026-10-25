# SPDX-License-Identifier: MIT
idf_component_register(SRCS "main.c" "app_state.c" "board_util.c" "decode.c" "eibi.c" "radio.c" "weather.c" "wifi.c" "web.c" "ui.c" "splash.c" "version.c"
                       INCLUDE_DIRS "."
                       REQUIRES si4732 band display encoder audio storage
                                battery rds esp_adc
                                esp_driver_i2c esp_driver_gpio esp_system
                                esp_wifi esp_netif esp_event esp_pm
                                esp_http_server app_update
                                esp_http_client esp-tls littlefs mbedtls)
