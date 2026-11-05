/* OTA transfer only: the light task keeps owning the PWM outputs.
 * Based on Espressif's CC0 esp_zigbee_ota/ota_client example (SDK 1.x).
 */
#include <string.h>
#include "esp_check.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "ota_client.h"
#include "zcl/esp_zigbee_zcl_command.h"

static const char *TAG = "OTA";
static const esp_partition_t *partition;
static esp_ota_handle_t handle;
static bool active, checked;
static uint8_t element_header[6];
static size_t header_used;
static uint32_t expected, received;

static esp_err_t reset_transfer(esp_err_t result)
{
    if (active) esp_ota_abort(handle);
    active = checked = false;
    partition = NULL;
    handle = 0;
    header_used = expected = received = 0;
    if (result != ESP_OK) ESP_LOGW(TAG, "Transfer rejected: %s", esp_err_to_name(result));
    return result;
}

static esp_err_t receive_data(const uint8_t *data, size_t length)
{
    if (!active || checked || (length && !data)) return ESP_ERR_INVALID_STATE;
    /* The stack removes the OTA file header, but retains the subelement header.
     * Accumulate it: it may be split across multiple radio blocks. */
    while (header_used < sizeof(element_header) && length) {
        element_header[header_used++] = *data++;
        --length;
    }
    if (header_used < sizeof(element_header)) return ESP_OK;
    if (!expected) {
        if (element_header[0] || element_header[1]) return ESP_ERR_NOT_SUPPORTED;
        expected = (uint32_t)element_header[2] | ((uint32_t)element_header[3] << 8) |
                   ((uint32_t)element_header[4] << 16) | ((uint32_t)element_header[5] << 24);
        if (!expected || expected > partition->size) return ESP_ERR_INVALID_SIZE;
    }
    if (length > expected - received) return ESP_ERR_INVALID_SIZE;
    if (length) {
        esp_err_t err = esp_ota_write(handle, data, length);
        if (err != ESP_OK) return err;
        received += length;
    }
    return ESP_OK;
}

esp_err_t ota_client_action(esp_zb_core_action_callback_id_t id, const void *message)
{
    if (!message) return ESP_ERR_INVALID_ARG;
    if (id == ESP_ZB_CORE_OTA_UPGRADE_QUERY_IMAGE_RESP_CB_ID) {
        const esp_zb_zcl_ota_upgrade_query_image_resp_message_t *m = message;
        if (m->info.status != ESP_ZB_ZCL_STATUS_SUCCESS ||
            m->query_status != ESP_ZB_ZCL_STATUS_SUCCESS) return ESP_OK;
        const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
        /* Only our single-subelement, 56-byte header format is accepted. */
        if (active || !next || m->manufacturer_code != APP_OTA_MANUFACTURER ||
            m->image_type != APP_OTA_IMAGE_TYPE || m->file_version <= APP_OTA_FILE_VERSION ||
            m->image_size <= 62 || m->image_size - 62 > next->size) return ESP_FAIL;
        ESP_LOGI(TAG, "Accepting version 0x%08lx (%lu bytes)",
                 (unsigned long)m->file_version, (unsigned long)m->image_size);
        return ESP_OK;
    }
    if (id != ESP_ZB_CORE_OTA_UPGRADE_VALUE_CB_ID) return ESP_OK;
    const esp_zb_zcl_ota_upgrade_value_message_t *m = message;
    if (m->info.status != ESP_ZB_ZCL_STATUS_SUCCESS) return reset_transfer(ESP_FAIL);
    esp_err_t err = ESP_OK;
    switch (m->upgrade_status) {
    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_START:
        reset_transfer(ESP_OK);
        partition = esp_ota_get_next_update_partition(NULL);
        if (!partition) return ESP_ERR_NOT_FOUND;
        /* Erase sectors as needed instead of erasing the entire slot at START. */
        err = esp_ota_begin(partition, OTA_WITH_SEQUENTIAL_WRITES, &handle);
        active = err == ESP_OK;
        ESP_LOGI(TAG, "Starting transfer into %s", partition->label);
        break;
    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_RECEIVE:
        err = receive_data(m->payload, m->payload_size);
        break;
    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_CHECK:
        if (!active || !expected || received != expected) {
            err = ESP_ERR_INVALID_SIZE;
            break;
        }
        /* Validate the ESP image BEFORE acknowledging a successful download. */
        err = esp_ota_end(handle);
        active = false; /* esp_ota_end frees the handle, even on error. */
        checked = err == ESP_OK;
        break;
    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_FINISH:
        if (!checked || !partition) return reset_transfer(ESP_ERR_INVALID_STATE);
        err = esp_ota_set_boot_partition(partition);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Image validated; rebooting. Normal startup output states apply.");
            esp_restart();
        }
        break;
    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_ABORT:
    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_ERROR:
        return reset_transfer(ESP_OK);
    default:
        break;
    }
    return err == ESP_OK ? ESP_OK : reset_transfer(err);
}

esp_err_t ota_client_add_cluster(esp_zb_cluster_list_t *clusters)
{
    esp_zb_ota_cluster_cfg_t cfg = {
        .ota_upgrade_file_version = APP_OTA_FILE_VERSION,
        .ota_upgrade_downloaded_file_ver = 0xffffffff,
        .ota_upgrade_manufacturer = APP_OTA_MANUFACTURER,
        .ota_upgrade_image_type = APP_OTA_IMAGE_TYPE,
    };
    static esp_zb_zcl_ota_upgrade_client_variable_t variables = {
        .timer_query = ESP_ZB_ZCL_OTA_UPGRADE_QUERY_TIMER_COUNT_DEF,
        .hw_version = 1,
        .max_data_size = 64,
    };
    static uint16_t server_address = 0xffff;
    static uint8_t server_endpoint = 0xff;
    esp_zb_attribute_list_t *ota = esp_zb_ota_cluster_create(&cfg);
    if (!ota) return ESP_ERR_NO_MEM;
    ESP_RETURN_ON_ERROR(esp_zb_ota_cluster_add_attr(ota, ESP_ZB_ZCL_ATTR_OTA_UPGRADE_CLIENT_DATA_ID,
                        &variables), TAG, "client variables");
    ESP_RETURN_ON_ERROR(esp_zb_ota_cluster_add_attr(ota, ESP_ZB_ZCL_ATTR_OTA_UPGRADE_SERVER_ADDR_ID,
                        &server_address), TAG, "server address");
    ESP_RETURN_ON_ERROR(esp_zb_ota_cluster_add_attr(ota, ESP_ZB_ZCL_ATTR_OTA_UPGRADE_SERVER_ENDPOINT_ID,
                        &server_endpoint), TAG, "server endpoint");
    return esp_zb_cluster_list_add_ota_cluster(clusters, ota, ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE);
}

bool ota_client_in_progress(void)
{
    return active || checked;
}

/* Rollback is enabled in the bootloader: a freshly installed image stays
 * "pending verify" until the application has proven it can reach the network. */
void ota_client_mark_valid(void)
{
    static bool done;
    if (done) return;
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    /* ESP_ERR_NOT_SUPPORTED / INVALID_STATE: image already valid (USB flash). */
    done = true;
    if (err == ESP_OK) ESP_LOGI(TAG, "Image marked valid; rollback cancelled");
}
