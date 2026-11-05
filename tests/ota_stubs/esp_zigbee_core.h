#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_INVALID_SIZE 3
#define ESP_ERR_NOT_FOUND 4
#define ESP_ERR_NO_MEM 5
#define ESP_ERR_NOT_SUPPORTED 6
#define ESP_ZB_ZCL_STATUS_SUCCESS 0
#define ESP_ZB_ZCL_OTA_UPGRADE_QUERY_TIMER_COUNT_DEF 1440
#define ESP_ZB_ZCL_ATTR_OTA_UPGRADE_CLIENT_DATA_ID 1
#define ESP_ZB_ZCL_ATTR_OTA_UPGRADE_SERVER_ADDR_ID 2
#define ESP_ZB_ZCL_ATTR_OTA_UPGRADE_SERVER_ENDPOINT_ID 3
#define ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE 0
#define OTA_WITH_SEQUENTIAL_WRITES 0xfffffffe

typedef enum { ESP_ZB_CORE_OTA_UPGRADE_VALUE_CB_ID, ESP_ZB_CORE_OTA_UPGRADE_QUERY_IMAGE_RESP_CB_ID } esp_zb_core_action_callback_id_t;
typedef enum {
 ESP_ZB_ZCL_OTA_UPGRADE_STATUS_START,
 ESP_ZB_ZCL_OTA_UPGRADE_STATUS_RECEIVE,
 ESP_ZB_ZCL_OTA_UPGRADE_STATUS_CHECK,
 ESP_ZB_ZCL_OTA_UPGRADE_STATUS_FINISH,
 ESP_ZB_ZCL_OTA_UPGRADE_STATUS_ABORT,
 ESP_ZB_ZCL_OTA_UPGRADE_STATUS_ERROR
} esp_zb_zcl_ota_upgrade_status_t;
typedef struct {int status;} info_t;
typedef struct {
 info_t info;
 int query_status;
 uint16_t manufacturer_code, image_type;
 uint32_t file_version, image_size;
} esp_zb_zcl_ota_upgrade_query_image_resp_message_t;
typedef struct {
 info_t info;
 esp_zb_zcl_ota_upgrade_status_t upgrade_status;
 uint16_t payload_size;
 uint8_t *payload;
} esp_zb_zcl_ota_upgrade_value_message_t;
typedef struct {
 uint32_t ota_upgrade_file_version, ota_upgrade_downloaded_file_ver;
 uint16_t ota_upgrade_manufacturer, ota_upgrade_image_type;
} esp_zb_ota_cluster_cfg_t;
typedef struct {uint16_t timer_query, hw_version; uint8_t max_data_size;} esp_zb_zcl_ota_upgrade_client_variable_t;
typedef int esp_zb_cluster_list_t;
typedef int esp_zb_attribute_list_t;
esp_zb_attribute_list_t *esp_zb_ota_cluster_create(esp_zb_ota_cluster_cfg_t *cfg);
esp_err_t esp_zb_ota_cluster_add_attr(esp_zb_attribute_list_t *, int, void *);
esp_err_t esp_zb_cluster_list_add_ota_cluster(esp_zb_cluster_list_t *, esp_zb_attribute_list_t *, int);
