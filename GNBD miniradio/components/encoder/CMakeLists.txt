# SPDX-License-Identifier: MIT
idf_component_register(SRCS "encoder.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_pcnt esp_driver_gpio)
