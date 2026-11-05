/*
 * light_engine.h — conversion ZCL -> rapports cycliques, sans matériel.
 * Compilable et testable sur PC (tests/test_light_engine.c).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define LIGHT_PWM_HZ_MIN        300
#define LIGHT_PWM_HZ_MAX        20000
#define LIGHT_PWM_HZ_DEFAULT    2000
#define LIGHT_PWM_BITS_MAX      13
#define LIGHT_CAP_PCT_MIN       10
#define LIGHT_CAP_PCT_MAX       100
#define LIGHT_CAP_PCT_DEFAULT   75      /* somme R+G+B <= 2,25 sur 3,0 */
#define LIGHT_LEVEL_MAX         254     /* niveau ZCL maximal */

typedef enum {
    LIGHT_COLOR_MODE_HS = 0,    /* ZCL ColorMode 0 : hue / saturation */
    LIGHT_COLOR_MODE_XY = 1,    /* ZCL ColorMode 1 : CIE xy */
} light_color_mode_t;

typedef struct {
    bool on;
    uint8_t level;              /* 0..254 */
    light_color_mode_t mode;
    uint16_t x, y;              /* CIE xy, 0..65535 = 0..1 */
    uint8_t hue, sat;           /* ZCL 0..254 */
} light_state_t;

typedef struct {
    uint16_t pwm_hz;            /* 300..20000 */
    uint8_t cap_pct;            /* 10..100 : plafond de R+G+B en % de 3,0 */
    bool rgbw;                  /* extrait W = min(R,G,B) (ruban RGBW) */
} light_cfg_t;

typedef struct {
    uint32_t r, g, b, w;
} light_duty_t;

void light_cfg_defaults(light_cfg_t *cfg);
/* Ramène chaque champ dans sa plage permise. */
void light_cfg_sanitize(light_cfg_t *cfg);
void light_state_defaults(light_state_t *st);

/* Bits de résolution LEDC utilisables à cette fréquence (<= 13). */
unsigned light_pwm_bits(uint32_t pwm_hz);

/* Courbe CIE 1931 : luminosité perçue 0..1 -> rapport cyclique linéaire 0..1. */
float light_cie1931(float v);

/* État ZCL -> rapports cycliques 0..max_duty pour R, G, B, W. */
void light_compute_rgbw(const light_state_t *st, const light_cfg_t *cfg,
                        uint32_t max_duty, light_duty_t *out);

/* Rondelles : on/off + niveau -> rapport cyclique 0..max_duty. */
uint32_t light_compute_dimmable(bool on, uint8_t level, uint32_t max_duty);
