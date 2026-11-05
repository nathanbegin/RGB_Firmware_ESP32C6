/*
 * zb_rgbw_controller.c — ESP32-C6-MINI-1-N4 (PCB ESP-C6-UNIT02)
 *
 * Endpoint 10 : Extended Color Light -> sorties R, G, B, W (IO18..IO21)
 * Endpoint 11 : Dimmable Light       -> rondelles P (IO22)
 *
 * Les callbacks Zigbee ne font que mettre à jour l'état partagé : le calcul des
 * rapports cycliques et l'écriture LEDC se font dans light_task.
 */

#include <string.h>
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

#include "esp_zigbee_core.h"
#include "zcl/esp_zigbee_zcl_command.h"
#include "ha/esp_zigbee_ha_standard.h"
#include "zb_rgbw_controller.h"
#include "app_config.h"
#include "light_engine.h"
#include "light_settings.h"
#include "pwm_driver.h"
#include "ota_client.h"

static const char *TAG = "ZB_RGBW";

/* ---------- État partagé ---------- */
static SemaphoreHandle_t s_mutex;
static light_saved_t s_saved;       /* protégé par s_mutex */
static light_cfg_t s_cfg;
static QueueHandle_t s_light_queue;
static atomic_bool s_outputs_enabled;   /* faux tant que la pile n'est pas prête */
static atomic_bool s_zb_ready;
static atomic_int s_ui_event;

enum { UI_NONE, UI_SEARCHING, UI_JOINED };

#define SAVE_IDLE_MS    2000

static void light_notify(void)
{
    uint8_t v = 1;
    if (s_light_queue) xQueueOverwrite(s_light_queue, &v);
}

/* ---------- Tâche lumière : calcul + LEDC + sauvegarde NVS différée ---------- */
static void light_apply(void)
{
    light_saved_t snap;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    snap = s_saved;
    xSemaphoreGive(s_mutex);

    light_duty_t d = {0};
    uint32_t washer = 0;
    if (atomic_load(&s_outputs_enabled)) {
        light_compute_rgbw(&snap.rgbw, &s_cfg, pwm_max_duty(), &d);
        washer = light_compute_dimmable(snap.washer_on, snap.washer_level, pwm_max_duty());
    }
    pwm_set_rgbw(&d);
    pwm_set_washer(washer);
    ESP_LOGI(TAG, "PWM R=%lu G=%lu B=%lu W=%lu P=%lu",
             (unsigned long)d.r, (unsigned long)d.g, (unsigned long)d.b,
             (unsigned long)d.w, (unsigned long)washer);
}

static void light_task(void *arg)
{
    (void)arg;
    bool save_pending = false;
    for (;;) {
        uint8_t v;
        TickType_t wait = save_pending ? pdMS_TO_TICKS(SAVE_IDLE_MS) : portMAX_DELAY;
        if (xQueueReceive(s_light_queue, &v, wait) == pdTRUE) {
            light_apply();
            save_pending = true;
        } else if (save_pending) {
            /* Plus de commande depuis 2 s : une seule écriture flash. */
            light_saved_t snap;
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            snap = s_saved;
            xSemaphoreGive(s_mutex);
            light_settings_save_state(&snap);
            save_pending = false;
        }
    }
}

/* ---------- Rapport d'état (lecture seule) ---------- */
static void report_on_off(uint8_t endpoint)
{
    esp_zb_zcl_report_attr_cmd_t cmd = {0};
    cmd.zcl_basic_cmd.src_endpoint = endpoint;
    cmd.zcl_basic_cmd.dst_endpoint = 1;
    cmd.zcl_basic_cmd.dst_addr_u.addr_short = 0x0000;
    cmd.address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT;
    cmd.clusterID = ESP_ZB_ZCL_CLUSTER_ID_ON_OFF;
    cmd.attributeID = ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID;
    cmd.direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI;
    esp_err_t err = esp_zb_zcl_report_attr_cmd_req(&cmd);
    if (err != ESP_OK) ESP_LOGW(TAG, "Rapport EP%u: %s", endpoint, esp_err_to_name(err));
}

/* ---------- Commandes entrantes ---------- */
static uint16_t attr_u16(const esp_zb_zcl_set_attr_value_message_t *m)
{
    return m->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_U16 ? *(uint16_t *)m->attribute.data.value : 0;
}

static uint8_t attr_u8(const esp_zb_zcl_set_attr_value_message_t *m)
{
    return *(uint8_t *)m->attribute.data.value;
}

static esp_err_t handle_rgbw_attr(const esp_zb_zcl_set_attr_value_message_t *m)
{
    esp_err_t ret = ESP_OK;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    light_state_t *st = &s_saved.rgbw;
    switch (m->info.cluster) {
    case ESP_ZB_ZCL_CLUSTER_ID_ON_OFF:
        if (m->attribute.id == ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID &&
            m->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_BOOL) {
            st->on = *(bool *)m->attribute.data.value;
        } else if (m->attribute.id == ESP_ZB_ZCL_ATTR_ON_OFF_START_UP_ON_OFF) {
            s_saved.startup_rgbw = attr_u8(m);
        }
        break;
    case ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL:
        if (m->attribute.id == ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_CURRENT_LEVEL_ID) {
            st->level = attr_u8(m);
        }
        break;
    case ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL:
        switch (m->attribute.id) {
        case ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_X_ID:
            st->x = attr_u16(m); st->mode = LIGHT_COLOR_MODE_XY; break;
        case ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_Y_ID:
            st->y = attr_u16(m); st->mode = LIGHT_COLOR_MODE_XY; break;
        case ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_HUE_ID:
            st->hue = attr_u8(m); st->mode = LIGHT_COLOR_MODE_HS; break;
        case ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_SATURATION_ID:
            st->sat = attr_u8(m); st->mode = LIGHT_COLOR_MODE_HS; break;
        case ESP_ZB_ZCL_ATTR_COLOR_CONTROL_COLOR_MODE_ID: {
            uint8_t mode = attr_u8(m);
            if (mode == 0) st->mode = LIGHT_COLOR_MODE_HS;
            else if (mode == 1) st->mode = LIGHT_COLOR_MODE_XY;
            break;
        }
        default:
            break;
        }
        break;
    default:
        ret = ESP_OK;
        break;
    }
    xSemaphoreGive(s_mutex);
    return ret;
}

static esp_err_t handle_washer_attr(const esp_zb_zcl_set_attr_value_message_t *m)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (m->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_ON_OFF) {
        if (m->attribute.id == ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID &&
            m->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_BOOL) {
            s_saved.washer_on = *(bool *)m->attribute.data.value;
        } else if (m->attribute.id == ESP_ZB_ZCL_ATTR_ON_OFF_START_UP_ON_OFF) {
            s_saved.startup_washer = attr_u8(m);
        }
    } else if (m->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL &&
               m->attribute.id == ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_CURRENT_LEVEL_ID) {
        s_saved.washer_level = attr_u8(m);
    }
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

static esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t cb_id, const void *message)
{
    if (cb_id == ESP_ZB_CORE_OTA_UPGRADE_VALUE_CB_ID ||
        cb_id == ESP_ZB_CORE_OTA_UPGRADE_QUERY_IMAGE_RESP_CB_ID) {
        return ota_client_action(cb_id, message);
    }
    if (cb_id != ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID) return ESP_OK;
    const esp_zb_zcl_set_attr_value_message_t *m = message;
    if (!m || m->info.status != ESP_ZB_ZCL_STATUS_SUCCESS || !m->attribute.data.value) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t ret = ESP_OK;
    if (m->info.dst_endpoint == HA_RGBW_ENDPOINT) {
        ret = handle_rgbw_attr(m);
    } else if (m->info.dst_endpoint == HA_WASHER_ENDPOINT) {
        ret = handle_washer_attr(m);
    } else {
        return ESP_OK;
    }
    light_notify();
    return ret;
}

/* ---------- Construction des endpoints ---------- */
static void add_identity(esp_zb_cluster_list_t *cl)
{
    esp_zb_attribute_list_t *basic = esp_zb_cluster_list_get_cluster(
        cl, ESP_ZB_ZCL_CLUSTER_ID_BASIC, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_basic_cluster_add_attr(basic, ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, MANUFACTURER_NAME);
    esp_zb_basic_cluster_add_attr(basic, ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, MODEL_IDENTIFIER);
}

static void add_startup_on_off(esp_zb_cluster_list_t *cl, uint8_t value)
{
    static uint8_t keep[2];
    static unsigned n;
    uint8_t *p = &keep[n++ % 2];
    *p = value;
    esp_zb_attribute_list_t *onoff = esp_zb_cluster_list_get_cluster(
        cl, ESP_ZB_ZCL_CLUSTER_ID_ON_OFF, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_on_off_cluster_add_attr(onoff, ESP_ZB_ZCL_ATTR_ON_OFF_START_UP_ON_OFF, p);
}

static esp_zb_ep_list_t *build_endpoints(const light_saved_t *boot)
{
    esp_zb_ep_list_t *ep_list = esp_zb_ep_list_create();

    /* --- EP10 : Extended Color Light --- */
    {
        esp_zb_color_dimmable_light_cfg_t cfg = ESP_ZB_DEFAULT_COLOR_DIMMABLE_LIGHT_CONFIG();
        cfg.on_off_cfg.on_off = boot->rgbw.on;
        cfg.level_cfg.current_level = boot->rgbw.level;
        cfg.color_cfg.current_x = boot->rgbw.x;
        cfg.color_cfg.current_y = boot->rgbw.y;
        cfg.color_cfg.color_mode = boot->rgbw.mode;
        cfg.color_cfg.enhanced_color_mode = boot->rgbw.mode;
        cfg.color_cfg.color_capabilities = COLOR_CAPABILITIES_HS_XY;
        esp_zb_cluster_list_t *cl = esp_zb_color_dimmable_light_clusters_create(&cfg);

        static uint8_t hue, sat;
        hue = boot->rgbw.hue;
        sat = boot->rgbw.sat;
        esp_zb_attribute_list_t *color = esp_zb_cluster_list_get_cluster(
            cl, ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
        esp_zb_color_control_cluster_add_attr(color, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_HUE_ID, &hue);
        esp_zb_color_control_cluster_add_attr(color, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_SATURATION_ID, &sat);

        add_identity(cl);
        add_startup_on_off(cl, boot->startup_rgbw);
        ESP_ERROR_CHECK(ota_client_add_cluster(cl));

        esp_zb_endpoint_config_t ep = {
            .endpoint = HA_RGBW_ENDPOINT,
            .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
            .app_device_id = EXTENDED_COLOR_LIGHT_DEVICE_ID,
            .app_device_version = 0,
        };
        ESP_ERROR_CHECK(esp_zb_ep_list_add_ep(ep_list, cl, ep));
    }

    /* --- EP11 : Dimmable Light (rondelles) --- */
    {
        esp_zb_dimmable_light_cfg_t cfg = ESP_ZB_DEFAULT_DIMMABLE_LIGHT_CONFIG();
        cfg.on_off_cfg.on_off = boot->washer_on;
        cfg.level_cfg.current_level = boot->washer_level;
        esp_zb_cluster_list_t *cl = esp_zb_dimmable_light_clusters_create(&cfg);
        add_startup_on_off(cl, boot->startup_washer);
        esp_zb_endpoint_config_t ep = {
            .endpoint = HA_WASHER_ENDPOINT,
            .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
            .app_device_id = ESP_ZB_HA_DIMMABLE_LIGHT_DEVICE_ID,
            .app_device_version = 0,
        };
        ESP_ERROR_CHECK(esp_zb_ep_list_add_ep(ep_list, cl, ep));
    }
    return ep_list;
}

/* ---------- Commissioning ---------- */
static void bdb_start_top_level_commissioning_cb(uint8_t mode_mask)
{
    ESP_ERROR_CHECK(esp_zb_bdb_start_top_level_commissioning(mode_mask));
}

/* Réseau disponible : les sorties peuvent suivre l'état restauré. */
static void network_ready(void)
{
    atomic_store(&s_zb_ready, true);
    atomic_store(&s_ui_event, UI_JOINED);
    atomic_store(&s_outputs_enabled, true);
    light_notify();
}

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *p_sg_p = signal_struct->p_app_signal;
    esp_err_t err_status = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t sig_type = *p_sg_p;

    switch (sig_type) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "Pile Zigbee initialisée");
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
        break;
    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (err_status == ESP_OK) {
            if (esp_zb_bdb_is_factory_new()) {
                ESP_LOGI(TAG, "Recherche d'un réseau à rejoindre...");
                atomic_store(&s_ui_event, UI_SEARCHING);
                esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
            } else {
                ESP_LOGI(TAG, "Déjà sur un réseau");
                network_ready();
            }
        } else {
            ESP_LOGW(TAG, "Échec init (%s), nouvel essai", esp_err_to_name(err_status));
            esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_top_level_commissioning_cb,
                                   ESP_ZB_BDB_MODE_INITIALIZATION, 1000);
        }
        break;
    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (err_status == ESP_OK) {
            ESP_LOGI(TAG, "Réseau rejoint, canal %d", esp_zb_get_current_channel());
            network_ready();
        } else {
            atomic_store(&s_zb_ready, false);
            atomic_store(&s_ui_event, UI_SEARCHING);
            ESP_LOGW(TAG, "Steering échoué (%s), nouvel essai", esp_err_to_name(err_status));
            esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_top_level_commissioning_cb,
                                   ESP_ZB_BDB_MODE_NETWORK_STEERING, 1000);
        }
        break;
    case ESP_ZB_ZDO_SIGNAL_LEAVE:
        if (err_status == ESP_OK) {
            atomic_store(&s_zb_ready, false);
            ESP_LOGI(TAG, "Réseau quitté; redémarrage");
            esp_restart();
        }
        break;
    default:
        ESP_LOGI(TAG, "Signal ZDO: %s (0x%x), status: %s",
                 esp_zb_zdo_signal_to_string(sig_type), sig_type, esp_err_to_name(err_status));
        break;
    }
}

/* ---------- Interface locale : DEL d'état + bouton PAIR ---------- */
static void status_led(bool on)
{
    gpio_set_level(STATUS_LED_GPIO, on ? 1 : 0);
}

static void blink(unsigned count, unsigned on_ms, unsigned off_ms)
{
    for (unsigned i = 0; i < count; ++i) {
        status_led(true);
        vTaskDelay(pdMS_TO_TICKS(on_ms));
        status_led(false);
        vTaskDelay(pdMS_TO_TICKS(off_ms));
    }
}

static void ui_task(void *arg)
{
    (void)arg;
    gpio_config_t led = {
        .mode = GPIO_MODE_OUTPUT, .pin_bit_mask = 1ULL << STATUS_LED_GPIO,
    };
    gpio_config(&led);
    status_led(false);
    gpio_config_t btn = {
        .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE,
        .pin_bit_mask = 1ULL << PAIR_BUTTON_GPIO,
    };
    gpio_config(&btn);

    TickType_t joined_at = 0, ready_since = 0;
    bool first_report_done = false;
    unsigned tick = 0;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(100));
        ++tick;
        TickType_t now = xTaskGetTickCount();

        /* PAIR (actif bas) : appui court = état réseau, 5 s = quitter + réappairer. */
        if (gpio_get_level(PAIR_BUTTON_GPIO) == 0) {
            TickType_t pressed = now;
            bool long_done = false;
            while (gpio_get_level(PAIR_BUTTON_GPIO) == 0) {
                vTaskDelay(pdMS_TO_TICKS(50));
                if (!long_done && xTaskGetTickCount() - pressed >= pdMS_TO_TICKS(PAIR_LONG_PRESS_MS)) {
                    long_done = true;
                    ESP_LOGW(TAG, "Appui long PAIR: effacement du réseau Zigbee");
                    blink(5, 60, 60);
                    esp_zb_lock_acquire(portMAX_DELAY);
                    esp_zb_factory_reset();     /* efface zb_storage et redémarre */
                    esp_zb_lock_release();
                }
            }
            if (!long_done) {
                if (atomic_load(&s_zb_ready)) blink(2, 400, 250);
                else blink(6, 80, 80);
            }
            continue;
        }

        bool in_ota = ota_client_in_progress();
        int ev = atomic_exchange(&s_ui_event, UI_NONE);
        if (ev == UI_JOINED) {
            joined_at = now;
            ready_since = now;
        }
        if (in_ota) {
            status_led((tick % 10) < 5);                 /* 1 Hz : OTA en cours */
        } else if (!atomic_load(&s_zb_ready)) {
            status_led((tick % 2) == 0);                 /* 5 Hz : recherche */
        } else if (joined_at && now - joined_at < pdMS_TO_TICKS(3000)) {
            status_led(true);                            /* fixe 3 s après appairage */
        } else {
            status_led(false);
        }

        /* Premier rapport puis validation de l'image OTA (retour arrière sinon). */
        if (!first_report_done && atomic_load(&s_zb_ready) && ready_since &&
            now - ready_since >= pdMS_TO_TICKS(FIRST_REPORT_DELAY_MS)) {
            esp_zb_lock_acquire(portMAX_DELAY);
            report_on_off(HA_RGBW_ENDPOINT);
            report_on_off(HA_WASHER_ENDPOINT);
            esp_zb_lock_release();
            ota_client_mark_valid();
            first_report_done = true;
        }
    }
}

/* ---------- Tâche Zigbee ---------- */
static void esp_zb_task(void *pv)
{
    (void)pv;
    esp_zb_cfg_t zb_cfg = ESP_ZB_ROUTER_CONFIG();
    esp_zb_init(&zb_cfg);

    light_saved_t boot;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    boot = s_saved;
    xSemaphoreGive(s_mutex);

    ESP_ERROR_CHECK(esp_zb_device_register(build_endpoints(&boot)));
    esp_zb_core_action_handler_register(zb_action_handler);
    esp_zb_set_primary_network_channel_set(ESP_ZB_PRIMARY_CHANNEL_MASK);
    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_stack_main_loop();
}

void app_main(void)
{
    /* PWM d'abord : sorties à 0, MOSFET bloqués, même en mode CONFIG. */
    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES || nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_ret);

    light_settings_load_cfg(&s_cfg);
    ESP_ERROR_CHECK(pwm_init(s_cfg.pwm_hz));

    if (app_config_get_boot_mode() == BOOT_MODE_CONFIG) {
        app_config_run_portal();    /* ne retourne pas */
        return;
    }

    s_mutex = xSemaphoreCreateMutex();
    s_light_queue = xQueueCreate(1, sizeof(uint8_t));
    if (!s_mutex || !s_light_queue) ESP_ERROR_CHECK(ESP_ERR_NO_MEM);

    light_settings_load_state(&s_saved);
    light_settings_apply_startup(&s_saved);   /* StartUpOnOff */
    ESP_LOGI(TAG, "Démarrage: RGBW %s, rondelles %s (sorties à 0 jusqu'au réseau)",
             s_saved.rgbw.on ? "ON" : "OFF", s_saved.washer_on ? "ON" : "OFF");

    if (xTaskCreate(light_task, "light_task", 4096, NULL, 5, NULL) != pdPASS ||
        xTaskCreate(ui_task, "ui_task", 3072, NULL, 3, NULL) != pdPASS) {
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }
    app_config_start_button_task();

    esp_zb_platform_config_t config = {
        .radio_config = ESP_ZB_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_ZB_DEFAULT_HOST_CONFIG(),
    };
    ESP_ERROR_CHECK(esp_zb_platform_config(&config));
    if (xTaskCreate(esp_zb_task, "Zigbee_main", 8192, NULL, 5, NULL) != pdPASS) {
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }
}
