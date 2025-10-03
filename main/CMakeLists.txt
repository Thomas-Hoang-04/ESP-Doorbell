file(GLOB_RECURSE SD_SOURCES "*.c")

idf_component_register(SRCS "main.c" ${SD_SOURCES}
                       PRIV_REQUIRES esp_driver_sdmmc esp_driver_gpio fatfs esp_psram esp_wifi esp_http_server mqtt nvs_flash esp_netif lwip
                       INCLUDE_DIRS "")
