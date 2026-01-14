# ==============================================================================
# ESP32 Doorbell - Component Registration
# ==============================================================================

# --- Storage & GPIO Drivers ---
set(DEPS_STORAGE esp_driver_sdmmc fatfs)
set(DEPS_GPIO esp_driver_gpio)
set(DEPS_TIMER esp_driver_gptimer esp_timer)

# --- I2S & Audio ---
set(DEPS_I2S esp_driver_i2s esp_driver_i2c)
set(DEPS_AUDIO esp_audio_codec esp_audio_effects)

# --- Video & Capture ---
set(DEPS_VIDEO esp_video_codec esp_capture esp_psram)

# --- Network & Connectivity ---
set(DEPS_NETWORK esp_wifi nvs_flash esp_netif lwip bt)
set(DEPS_MQTT mqtt)
set(DEPS_WEBSOCKET esp_websocket_client)

# --- Utilities ---
set(DEPS_JSON json)

# --- Aggregate All Dependencies ---
set(DEPS
    ${DEPS_STORAGE}
    ${DEPS_GPIO}
    ${DEPS_TIMER}
    ${DEPS_I2S}
    ${DEPS_AUDIO}
    ${DEPS_VIDEO}
    ${DEPS_NETWORK}
    ${DEPS_MQTT}
    ${DEPS_WEBSOCKET}
    ${DEPS_JSON}
)

# --- Source Files ---
file(GLOB_RECURSE SOURCES "*.c")

# --- Register Component ---
idf_component_register(SRCS "main.c" ${SOURCES}
                       PRIV_REQUIRES ${DEPS}
                       INCLUDE_DIRS ""
                       EMBED_TXTFILES "network/certs/ca.pem"
                                      "network/certs/esp32_client.pem"
                                      "network/certs/esp32_client.key")

