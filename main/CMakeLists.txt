set(DEPS esp_driver_sdmmc esp_driver_gpio fatfs esp_psram esp_wifi
        nvs_flash esp_netif lwip esp_driver_i2s esp_timer
        esp_audio_codec esp_audio_effects esp_capture esp_driver_i2c
        esp_video_codec esp_websocket_client mqtt json)

file(GLOB_RECURSE SOURCES "*.c")

idf_component_register(SRCS "main.c" ${SOURCES}
                       PRIV_REQUIRES ${DEPS}
                       INCLUDE_DIRS "")
