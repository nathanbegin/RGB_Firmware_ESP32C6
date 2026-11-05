/*
 * light_settings.h — réglages et dernier état, conservés en NVS.
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "light_engine.h"

/* Valeurs de StartUpOnOff (ZCL 0x4003) */
#define STARTUP_OFF      0x00
#define STARTUP_ON       0x01
#define STARTUP_TOGGLE   0x02
#define STARTUP_PREVIOUS 0xFF

typedef struct {
    light_state_t rgbw;         /* endpoint 10 */
    bool washer_on;             /* endpoint 11 */
    uint8_t washer_level;
    uint8_t startup_rgbw;       /* StartUpOnOff de chaque endpoint */
    uint8_t startup_washer;
} light_saved_t;

void light_settings_load_cfg(light_cfg_t *cfg);
esp_err_t light_settings_save_cfg(const light_cfg_t *cfg);

void light_settings_load_state(light_saved_t *st);
esp_err_t light_settings_save_state(const light_saved_t *st);

/* Applique StartUpOnOff à l'état sauvegardé pour obtenir l'état de démarrage. */
void light_settings_apply_startup(light_saved_t *st);
