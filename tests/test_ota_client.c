#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ota_client.h"
#include "esp_ota_ops.h"

static esp_partition_t slot = {0x1E0000, "ota_1"};
static bool missing, write_error, end_error, boot_error;
static unsigned writes, ends, aborts, boots, restarts;
static uint8_t stored[32];
static size_t stored_size;
void test_log(const char *tag, const char *fmt, ...) {(void)tag; (void)fmt;}
const char *esp_err_to_name(esp_err_t e) {(void)e; return "error";}
void esp_restart(void) {++restarts;}
static unsigned valids;
esp_err_t esp_ota_mark_app_valid_cancel_rollback(void) {++valids; return ESP_OK;}
const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *p) {(void)p; return missing ? NULL : &slot;}
esp_err_t esp_ota_begin(const esp_partition_t *p, size_t size, esp_ota_handle_t *h) {
 assert(p == &slot && size == OTA_WITH_SEQUENTIAL_WRITES); *h = 1; stored_size = 0; return ESP_OK;
}
esp_err_t esp_ota_write(esp_ota_handle_t h, const void *p, size_t n) {
 assert(h == 1); ++writes; if (write_error) return ESP_FAIL;
 assert(stored_size + n <= sizeof(stored)); memcpy(stored + stored_size, p, n); stored_size += n; return ESP_OK;
}
esp_err_t esp_ota_end(esp_ota_handle_t h) {assert(h == 1); ++ends; return end_error ? ESP_FAIL : ESP_OK;}
esp_err_t esp_ota_abort(esp_ota_handle_t h) {assert(h == 1); ++aborts; return ESP_OK;}
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *p) {assert(p == &slot); ++boots; return boot_error ? ESP_FAIL : ESP_OK;}
esp_zb_attribute_list_t *esp_zb_ota_cluster_create(esp_zb_ota_cluster_cfg_t *cfg) {static int a; assert(cfg->ota_upgrade_file_version == APP_OTA_FILE_VERSION); return &a;}
esp_err_t esp_zb_ota_cluster_add_attr(esp_zb_attribute_list_t *a, int id, void *p) {(void)a;(void)id;(void)p;return ESP_OK;}
esp_err_t esp_zb_cluster_list_add_ota_cluster(esp_zb_cluster_list_t *a, esp_zb_attribute_list_t *b, int role) {(void)a;(void)b;assert(role == ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE);return ESP_OK;}

static esp_err_t event(esp_zb_zcl_ota_upgrade_status_t status, uint8_t *data, size_t n) {
 esp_zb_zcl_ota_upgrade_value_message_t m = {.info = {0}, .upgrade_status = status, .payload = data, .payload_size = n};
 return ota_client_action(ESP_ZB_CORE_OTA_UPGRADE_VALUE_CB_ID, &m);
}
#define START() assert(event(ESP_ZB_ZCL_OTA_UPGRADE_STATUS_START, NULL, 0) == ESP_OK)
#define CHECK() event(ESP_ZB_ZCL_OTA_UPGRADE_STATUS_CHECK, NULL, 0)
#define FINISH() event(ESP_ZB_ZCL_OTA_UPGRADE_STATUS_FINISH, NULL, 0)
#define RECEIVE(p,n) event(ESP_ZB_ZCL_OTA_UPGRADE_STATUS_RECEIVE, p, n)
int main(void) {
 uint8_t image[] = {0,0,4,0,0,0,0xe9,2,3,4};
 // Every possible split, including within the element header, writes exactly the application.
 for (size_t split = 0; split <= sizeof(image); ++split) {
  START(); assert(RECEIVE(image, split) == ESP_OK);
  assert(RECEIVE(image + split, sizeof(image) - split) == ESP_OK);
  assert(stored_size == 4 && !memcmp(stored, image + 6, 4));
  unsigned before = boots; assert(CHECK() == ESP_OK); assert(boots == before);
  assert(FINISH() == ESP_OK && boots == before + 1 && restarts == boots);
 }
 unsigned before = boots;
 START(); assert(RECEIVE(image, 8) == ESP_OK); assert(CHECK() != ESP_OK); assert(FINISH() != ESP_OK);
 START(); image[0] = 1; assert(RECEIVE(image, 10) != ESP_OK); image[0] = 0;
 START(); image[5] = 1; assert(RECEIVE(image, 10) != ESP_OK); image[5] = 0;
 START(); assert(RECEIVE(image, 10) == ESP_OK); assert(RECEIVE(image, 1) != ESP_OK);
 START(); write_error = true; assert(RECEIVE(image, 10) != ESP_OK); write_error = false;
 START(); assert(RECEIVE(image, 10) == ESP_OK); end_error = true; assert(CHECK() != ESP_OK); end_error = false; assert(FINISH() != ESP_OK);
 START(); assert(RECEIVE(image, 8) == ESP_OK); assert(event(ESP_ZB_ZCL_OTA_UPGRADE_STATUS_ABORT, NULL, 0) == ESP_OK); assert(FINISH() != ESP_OK);
 START(); assert(RECEIVE(image, 10) == ESP_OK); assert(CHECK() == ESP_OK); boot_error = true; assert(FINISH() != ESP_OK); boot_error = false;
 assert(boots == before + 1 && restarts == before); // Failed boot selection never restarts.
 missing = true; assert(event(ESP_ZB_ZCL_OTA_UPGRADE_STATUS_START, NULL, 0) != ESP_OK); missing = false;
 esp_zb_zcl_ota_upgrade_query_image_resp_message_t q = {.info={0}, .manufacturer_code=APP_OTA_MANUFACTURER, .image_type=APP_OTA_IMAGE_TYPE, .file_version=APP_OTA_FILE_VERSION+1, .image_size=66};
 assert(ota_client_action(ESP_ZB_CORE_OTA_UPGRADE_QUERY_IMAGE_RESP_CB_ID, &q) == ESP_OK);
 q.file_version--; assert(ota_client_action(ESP_ZB_CORE_OTA_UPGRADE_QUERY_IMAGE_RESP_CB_ID, &q) != ESP_OK); q.file_version++;
 q.manufacturer_code++; assert(ota_client_action(ESP_ZB_CORE_OTA_UPGRADE_QUERY_IMAGE_RESP_CB_ID, &q) != ESP_OK); q.manufacturer_code--;
 q.image_type++; assert(ota_client_action(ESP_ZB_CORE_OTA_UPGRADE_QUERY_IMAGE_RESP_CB_ID, &q) != ESP_OK); q.image_type--;
 q.image_size = slot.size + 63; assert(ota_client_action(ESP_ZB_CORE_OTA_UPGRADE_QUERY_IMAGE_RESP_CB_ID, &q) != ESP_OK);
 assert(writes && ends && aborts);
 ota_client_mark_valid(); ota_client_mark_valid(); assert(valids == 1);
 puts("PASS: OTA fragmented blocks, truncation, invalid tags/sizes, write/validation/boot failures, abort/retry, image identity");
}
