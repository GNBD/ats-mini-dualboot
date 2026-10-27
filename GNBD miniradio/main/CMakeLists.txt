# SPDX-License-Identifier: MIT
idf_component_register(SRCS "main.c" "menu.c" "input.c" "pages.c" "time.c" "wifi_ui.c" "lists.c" "keypad.c" "save.c" "webcmd.c" "setup.c" "ui/ui_core.c" "ui/ui_asset.c" "ui/ui_exlu.c" "ui/ui_exlu_app.c" "ui/ui_lua.c" "ui/ui_freq.c" "ui/ui_home.c" "ui/ui_spectrum.c" "ui/ui_quick.c" "ui/ui_dx.c" "ui/ui_list.c" "ui/ui_wifi.c" "ui/ui_misc.c" "app_state.c" "board_util.c" "decode.c" "eibi.c" "radio.c" "weather.c" "wifi.c" "web.c" "ui/ui_custom.c" "menu_data.c" "settings_data.c" "splash.c" "version.c" "mod_runtime.c" "ui/ui_runtime.c" "ui/ui_web.c" "ui/ui_editor_assets.c"
                       INCLUDE_DIRS "." "ui"
                       REQUIRES si4732 band display encoder audio storage
                                battery rds esp_adc
                                esp_driver_i2c esp_driver_gpio esp_system
                                esp_wifi esp_netif esp_event esp_pm
                                esp_http_server app_update
                                esp_http_client esp-tls littlefs mbedtls lua)

target_compile_options(__idf_main PRIVATE -Wno-error=format-truncation)
