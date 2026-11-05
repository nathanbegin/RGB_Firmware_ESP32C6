#pragma once
#include <stdbool.h>
#include "esp_zigbee_core.h"

/* Keep these IDs stable; increment FILE_VERSION for every released image. */
#define APP_OTA_MANUFACTURER 0x131B
#define APP_OTA_IMAGE_TYPE 0x0201
#define APP_OTA_FILE_VERSION 0x00000001

esp_err_t ota_client_add_cluster(esp_zb_cluster_list_t *clusters);
esp_err_t ota_client_action(esp_zb_core_action_callback_id_t id, const void *message);
/* True while a transfer is being received or waiting to be activated. */
bool ota_client_in_progress(void);
/* Call once the device has joined the network and answered a first report. */
void ota_client_mark_valid(void);
