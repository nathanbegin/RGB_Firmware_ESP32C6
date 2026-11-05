/*
 * app_config.c — mode CONFIG : SoftAP "NathanSensors-Setup" + page de réglage
 * (fréquence PWM, plafond de puissance R+G+B, option ruban RGBW).
 *
 * Le passage Zigbee <-> Wi-Fi se fait TOUJOURS par reboot (drapeau NVS),
 * jamais à chaud, pour éviter les conflits de radio 2.4 GHz.
 * En mode CONFIG, les sorties PWM restent à 0 (MOSFET bloqués).
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/gpio.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_server.h"

#include <stdio.h>
#include <stdlib.h>
#include "app_config.h"
#include "board_config.h"
#include "light_settings.h"

static const char *TAG = "APP_CFG";

#define NVS_NAMESPACE   "appcfg"
#define NVS_KEY_BOOT    "boot_mode"

/* ============================================================
 *  Mode de démarrage (NVS)
 * ============================================================ */
boot_mode_t app_config_get_boot_mode(void)
{
    nvs_handle_t h;
    uint8_t v = BOOT_MODE_NORMAL;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, NVS_KEY_BOOT, &v);
        nvs_close(h);
    }
    return (boot_mode_t)v;
}

void app_config_set_boot_mode(boot_mode_t mode)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, NVS_KEY_BOOT, (uint8_t)mode);
        nvs_commit(h);
        nvs_close(h);
        ESP_LOGI(TAG, "boot_mode écrit en NVS: %d", mode);
    } else {
        ESP_LOGE(TAG, "Impossible d'ouvrir la NVS pour écrire boot_mode");
    }
}

/* ============================================================
 *  Bouton BOOT : appui long -> mode CONFIG + reboot
 * ============================================================ */
static void config_button_task(void *arg)
{
    gpio_config_t io = {
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
        .pin_bit_mask = (1ULL << CONFIG_BUTTON_GPIO),
    };
    gpio_config(&io);

    ESP_LOGI(TAG, "Surveillance bouton BOOT (GPIO%d) — appui long %d ms = mode config",
             CONFIG_BUTTON_GPIO, CONFIG_BUTTON_HOLD_MS);

    while (1) {
        /* bouton appuyé = niveau bas */
        if (gpio_get_level(CONFIG_BUTTON_GPIO) == 0) {
            TickType_t pressed_at = xTaskGetTickCount();
            bool triggered = false;
            while (gpio_get_level(CONFIG_BUTTON_GPIO) == 0) {
                vTaskDelay(pdMS_TO_TICKS(100));
                if (!triggered &&
                    (xTaskGetTickCount() - pressed_at) >= pdMS_TO_TICKS(CONFIG_BUTTON_HOLD_MS)) {
                    triggered = true;
                    ESP_LOGW(TAG, "Appui long détecté -> bascule en mode CONFIG");
                    app_config_set_boot_mode(BOOT_MODE_CONFIG);
                    vTaskDelay(pdMS_TO_TICKS(300));
                    esp_restart();
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void app_config_start_button_task(void)
{
    xTaskCreate(config_button_task, "cfg_btn", 3072, NULL, 4, NULL);
}

/* ============================================================
 *  Portail Wi-Fi (mode CONFIG)
 * ============================================================ */

static const char *PAGE_FMT =
    "<!DOCTYPE html><html lang='fr'><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>ESP-C6-UNIT02 — Réglages</title>"
    "<style>body{font-family:system-ui,sans-serif;margin:0;background:#0f1115;color:#e6e6e6}"
    ".wrap{max-width:520px;margin:0 auto;padding:24px}h1{font-size:1.3rem}"
    ".card{background:#1b1e26;border:1px solid #2a2f3a;border-radius:12px;padding:18px;margin:16px 0}"
    "label{display:block;margin-top:12px}input[type=number]{width:100%%;padding:10px;box-sizing:border-box}"
    ".btn{display:block;width:100%%;padding:14px;border:0;border-radius:10px;font-size:1rem;"
    "font-weight:600;cursor:pointer;margin-top:14px;background:#3b82f6;color:#fff}"
    ".muted{color:#9aa3b2;font-size:.9rem}</style></head><body><div class='wrap'>"
    "<h1>ESP-C6-UNIT02 — Réglages</h1>"
    "<div class='card'><form action='/save' method='post'>"
    "<label>Fréquence PWM (Hz, %d–%d)<input type='number' name='hz' min='%d' max='%d' value='%u'></label>"
    "<label>Plafond R+G+B (%% de 3,0, %d–%d)<input type='number' name='cap' min='%d' max='%d' value='%u'></label>"
    "<label><input type='checkbox' name='rgbw' value='1' %s> Ruban RGBW (extraire le blanc sur W)</label>"
    "<p class='muted'>Mesurer le courant réel à R=G=B=100 %% puis ajuster le plafond (75 %% par défaut).</p>"
    "<button class='btn' type='submit'>Enregistrer</button></form></div>"
    "<div class='card'><form action='/exit' method='post'>"
    "<button class='btn' type='submit'>Terminer et revenir au Zigbee</button></form></div>"
    "</div></body></html>";

static esp_err_t root_get_handler(httpd_req_t *req)
{
    light_cfg_t cfg;
    light_settings_load_cfg(&cfg);
    char *page = malloc(2600);
    if (!page) return httpd_resp_send_500(req);
    snprintf(page, 2600, PAGE_FMT,
             LIGHT_PWM_HZ_MIN, LIGHT_PWM_HZ_MAX, LIGHT_PWM_HZ_MIN, LIGHT_PWM_HZ_MAX,
             (unsigned)cfg.pwm_hz,
             LIGHT_CAP_PCT_MIN, LIGHT_CAP_PCT_MAX, LIGHT_CAP_PCT_MIN, LIGHT_CAP_PCT_MAX,
             (unsigned)cfg.cap_pct, cfg.rgbw ? "checked" : "");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, page);
    free(page);
    return ESP_OK;
}

/* Corps application/x-www-form-urlencoded : hz=2000&cap=75&rgbw=1 */
static esp_err_t save_post_handler(httpd_req_t *req)
{
    char body[96];
    if (req->content_len == 0 || req->content_len >= sizeof(body)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Requête invalide");
    }
    int n = httpd_req_recv(req, body, req->content_len);
    if (n <= 0) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Requête invalide");
    body[n] = '\0';

    light_cfg_t cfg;
    light_settings_load_cfg(&cfg);
    char val[12];
    if (httpd_query_key_value(body, "hz", val, sizeof(val)) == ESP_OK) cfg.pwm_hz = (uint16_t)atoi(val);
    if (httpd_query_key_value(body, "cap", val, sizeof(val)) == ESP_OK) cfg.cap_pct = (uint8_t)atoi(val);
    cfg.rgbw = httpd_query_key_value(body, "rgbw", val, sizeof(val)) == ESP_OK;
    light_cfg_sanitize(&cfg);   /* hors plage -> ramené dans la plage */
    esp_err_t err = light_settings_save_cfg(&cfg);
    ESP_LOGI(TAG, "Réglages: %u Hz, plafond %u %%, rgbw=%d (%s)", (unsigned)cfg.pwm_hz,
             (unsigned)cfg.cap_pct, cfg.rgbw, esp_err_to_name(err));
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t exit_post_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req,
        "<html><body style='font-family:sans-serif;background:#0f1115;color:#e6e6e6;"
        "text-align:center;padding-top:60px'>"
        "<h2>Retour au mode Zigbee…</h2><p>L'appareil redémarre.</p></body></html>");
    ESP_LOGI(TAG, "Sortie du mode config -> NORMAL + reboot");
    app_config_set_boot_mode(BOOT_MODE_NORMAL);
    vTaskDelay(pdMS_TO_TICKS(800));
    esp_restart();
    return ESP_OK;
}

static esp_err_t captive_redirect_handler(httpd_req_t *req, httpd_err_code_t err)
{
    (void)err;
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Échec démarrage serveur web");
        return NULL;
    }
    httpd_uri_t root = {.uri = "/", .method = HTTP_GET, .handler = root_get_handler};
    httpd_uri_t save = {.uri = "/save", .method = HTTP_POST, .handler = save_post_handler};
    httpd_uri_t exit_uri = {.uri = "/exit", .method = HTTP_POST, .handler = exit_post_handler};
    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &save);
    httpd_register_uri_handler(server, &exit_uri);
    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, captive_redirect_handler);
    ESP_LOGI(TAG, "Serveur web démarré");
    return server;
}

static void wifi_init_softap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t ap = {
        .ap = {
            .ssid = CONFIG_AP_SSID,
            .ssid_len = strlen(CONFIG_AP_SSID),
            .channel = CONFIG_AP_CHANNEL,
            .max_connection = CONFIG_AP_MAX_CONN,
            .authmode = WIFI_AUTH_OPEN,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "SoftAP actif : SSID='%s' — http://192.168.4.1", CONFIG_AP_SSID);
}

void app_config_run_portal(void)
{
    ESP_LOGI(TAG, "=== MODE CONFIGURATION (Wi-Fi) ===");
    wifi_init_softap();
    start_webserver();
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
