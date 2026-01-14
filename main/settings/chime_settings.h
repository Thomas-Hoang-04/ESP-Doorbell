#ifndef CHIME_SETTINGS_H
#define CHIME_SETTINGS_H

#include "esp_err.h"

#define CHIME_SETTINGS_TAG "CHIME_SETTINGS"
#define CHIME_MIN_INDEX 1
#define CHIME_MAX_INDEX 4
#define CHIME_DEFAULT_INDEX 1

esp_err_t chime_settings_init(void);
int chime_settings_get_index(void);
esp_err_t chime_settings_set_index(int index);

#endif
