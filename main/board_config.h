/*
 * board_config.h — brochage du PCB ESP-C6-UNIT02 (ESP32-C6-MINI-1-N4)
 * Source : dossier de conception « Contrôleur Zigbee RGBW + 8 rondelles »,
 * sections 3.1 et 3.4. Toute modification de PCB passe par ce fichier.
 *
 * Chaîne par sortie : GPIO -> 74AHCT244 (5 V) -> 470 ohm -> AON7410 côté bas.
 * Le tampon est NON inverseur : GPIO haut = MOSFET passant = charge allumée.
 */
#pragma once

/* ---------- Sorties PWM ---------- */
#define PWM_GPIO_R      18      /* IO18 -> B1 broche 2  -> U4  (J3 R-) */
#define PWM_GPIO_G      19      /* IO19 -> B1 broche 4  -> U6  (J3 G-) */
#define PWM_GPIO_B      20      /* IO20 -> B1 broche 6  -> U8  (J3 B-) */
#define PWM_GPIO_W      21      /* IO21 -> B1 broche 8  -> U9  (J3 W-) */
#define PWM_GPIO_P      22      /* IO22 -> B1 broche 17 -> U10 (J5 P-, rondelles) */

/* ---------- Interface locale ---------- */
#define STATUS_LED_GPIO 23      /* DEL verte D5, active HAUT (1 kohm vers GND) */
#define PAIR_BUTTON_GPIO 2      /* SW2 PAIR vers GND, rappel 10 kohm externe */
#define CONFIG_BUTTON_GPIO 9    /* SW3 BOOT vers GND (broche d'amorçage) */

#define PAIR_LONG_PRESS_MS   5000   /* quitte le réseau, efface zb_storage */
#define CONFIG_BUTTON_HOLD_MS 3000  /* portail Wi-Fi de réglage */

/* ---------- Endpoints Zigbee ---------- */
#define HA_RGBW_ENDPOINT    10      /* Extended Color Light (0x010D) : R, G, B, W */
#define HA_WASHER_ENDPOINT  11      /* Dimmable Light (0x0101) : rondelles */

/* ---------- Identité Zigbee (chaînes ZCL : 1er octet = longueur) ---------- */
#define MANUFACTURER_NAME   "\x0D""NathanSensors"
#define MODEL_IDENTIFIER    "\x0D""ESP-C6-UNIT02"
