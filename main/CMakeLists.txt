set(DEPS esp_driver_sdmmc esp_driver_gpio fatfs esp_psram esp_wifi
        esp_http_server mqtt nvs_flash esp_netif lwip esp_driver_i2s
        esp_timer esp_audio_codec esp_audio_effects esp_capture esp_driver_i2c)

file(GLOB_RECURSE SOURCES "*.c")
file(GLOB_RECURSE IGNORE_SOURCES "*/test/*.c")
list(REMOVE_ITEM SOURCES ${IGNORE_SOURCES})

idf_component_register(SRCS "main.c" ${SOURCES}
                       PRIV_REQUIRES ${DEPS}
                       INCLUDE_DIRS "")
