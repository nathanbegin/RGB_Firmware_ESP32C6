/*
 * zb_rgbw_controller.h — ESP-C6-UNIT02 : routeur Zigbee, 2 luminaires.
 *   EP10 Extended Color Light (0x010D) : R, G, B, W
 *   EP11 Dimmable Light (0x0101)       : rondelles (P)
 */
#pragma once

#include "esp_zigbee_core.h"
#include "board_config.h"

/* Alimenté en permanence -> routeur (renforce le maillage). */
#define ESP_ZB_ROUTER_CONFIG()                                  \
    {                                                           \
        .esp_zb_role = ESP_ZB_DEVICE_TYPE_ROUTER,               \
        .install_code_policy = false,                           \
        .nwk_cfg.zczr_cfg = {                                   \
            .max_children = 10,                                 \
        },                                                      \
    }

#define ESP_ZB_DEFAULT_RADIO_CONFIG()                           \
    {                                                           \
        .radio_mode = ZB_RADIO_MODE_NATIVE,                     \
    }

#define ESP_ZB_DEFAULT_HOST_CONFIG()                            \
    {                                                           \
        .host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE,   \
    }

#define ESP_ZB_PRIMARY_CHANNEL_MASK     ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK

/* ZCL : ColorCapabilities = HS (bit 0) + XY (bit 3). */
#define COLOR_CAPABILITIES_HS_XY        0x0009
#define EXTENDED_COLOR_LIGHT_DEVICE_ID  0x010D

/* Attente avant le premier rapport / validation de l'image OTA (ms) */
#define FIRST_REPORT_DELAY_MS           10000
