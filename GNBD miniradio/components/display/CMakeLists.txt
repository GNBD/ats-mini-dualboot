# SPDX-License-Identifier: MIT
idf_component_register(SRCS "display.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES lovyan_gfx esp_driver_ledc esp_driver_gpio driver)
