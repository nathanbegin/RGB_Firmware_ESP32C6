#include <string.h>
#include "esp_log.h"
#include "nvs.h"
#include "light_settings.h"

static const char *TAG = "SETTINGS";
#define NS "light"

void light_settings_load_cfg(light_cfg_t *cfg)
{
    light_cfg_defaults(cfg);
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return;
    uint16_t hz;
    uint8_t cap, rgbw;
    if (nvs_get_u16(h, "pwm_hz", &hz) == ESP_OK) cfg->pwm_hz = hz;
    if (nvs_get_u8(h, "cap_pct", &cap) == ESP_OK) cfg->cap_pct = cap;
    if (nvs_get_u8(h, "rgbw", &rgbw) == ESP_OK) cfg->rgbw = rgbw != 0;
    nvs_close(h);
    light_cfg_sanitize(cfg);
}

esp_err_t light_settings_save_cfg(const light_cfg_t *cfg)
{
    light_cfg_t c = *cfg;
    light_cfg_sanitize(&c);
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_u16(h, "pwm_hz", c.pwm_hz);
    if (err == ESP_OK) err = nvs_set_u8(h, "cap_pct", c.cap_pct);
    if (err == ESP_OK) err = nvs_set_u8(h, "rgbw", c.rgbw ? 1 : 0);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) ESP_LOGE(TAG, "Sauvegarde réglages: %s", esp_err_to_name(err));
    return err;
}

static void saved_defaults(light_saved_t *st)
{
    memset(st, 0, sizeof(*st));
    light_state_defaults(&st->rgbw);
    st->washer_level = LIGHT_LEVEL_MAX;
    st->startup_rgbw = st->startup_washer = STARTUP_PREVIOUS;
}

void light_settings_load_state(light_saved_t *st)
{
    saved_defaults(st);
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return;
    light_saved_t tmp;
    size_t len = sizeof(tmp);
    /* Blob de taille différente (autre version) : ignoré, valeurs par défaut. */
    if (nvs_get_blob(h, "state", &tmp, &len) == ESP_OK && len == sizeof(tmp) &&
        tmp.rgbw.mode <= LIGHT_COLOR_MODE_XY && tmp.rgbw.level <= LIGHT_LEVEL_MAX &&
        tmp.washer_level <= LIGHT_LEVEL_MAX) {
        *st = tmp;
    }
    nvs_close(h);
}

esp_err_t light_settings_save_state(const light_saved_t *st)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(h, "state", st, sizeof(*st));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) ESP_LOGE(TAG, "Sauvegarde état: %s", esp_err_to_name(err));
    return err;
}

static bool startup_on(uint8_t mode, bool previous)
{
    switch (mode) {
    case STARTUP_OFF: return false;
    case STARTUP_ON: return true;
    case STARTUP_TOGGLE: return !previous;
    default: return previous;
    }
}

void light_settings_apply_startup(light_saved_t *st)
{
    st->rgbw.on = startup_on(st->startup_rgbw, st->rgbw.on);
    st->washer_on = startup_on(st->startup_washer, st->washer_on);
}
