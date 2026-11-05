/*
 * app_config.h — démarrage NORMAL (Zigbee) ou CONFIG (portail Wi-Fi de réglage).
 * Le passage de l'un à l'autre se fait toujours par redémarrage (drapeau NVS).
 */
#pragma once

#include <stdbool.h>

#define CONFIG_AP_SSID      "NathanSensors-Setup"
#define CONFIG_AP_CHANNEL   1
#define CONFIG_AP_MAX_CONN  4

typedef enum {
    BOOT_MODE_NORMAL = 0,
    BOOT_MODE_CONFIG = 1,
} boot_mode_t;

boot_mode_t app_config_get_boot_mode(void);
void app_config_set_boot_mode(boot_mode_t mode);

/* Surveille BOOT (appui long) -> mode CONFIG + redémarrage. Mode NORMAL seulement. */
void app_config_start_button_task(void);

/* Portail Wi-Fi : fréquence PWM, plafond de puissance, option RGBW. Ne retourne pas. */
void app_config_run_portal(void);
