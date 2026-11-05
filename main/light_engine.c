#include "light_engine.h"
#include <math.h>

void light_cfg_defaults(light_cfg_t *cfg)
{
    cfg->pwm_hz = LIGHT_PWM_HZ_DEFAULT;
    cfg->cap_pct = LIGHT_CAP_PCT_DEFAULT;
    cfg->rgbw = false;
}

void light_cfg_sanitize(light_cfg_t *cfg)
{
    if (cfg->pwm_hz < LIGHT_PWM_HZ_MIN) cfg->pwm_hz = LIGHT_PWM_HZ_MIN;
    if (cfg->pwm_hz > LIGHT_PWM_HZ_MAX) cfg->pwm_hz = LIGHT_PWM_HZ_MAX;
    if (cfg->cap_pct < LIGHT_CAP_PCT_MIN) cfg->cap_pct = LIGHT_CAP_PCT_MIN;
    if (cfg->cap_pct > LIGHT_CAP_PCT_MAX) cfg->cap_pct = LIGHT_CAP_PCT_MAX;
}

void light_state_defaults(light_state_t *st)
{
    st->on = false;
    st->level = LIGHT_LEVEL_MAX;
    st->mode = LIGHT_COLOR_MODE_XY;
    st->x = 0x616B;     /* ~D65 : x = 0,3794 */
    st->y = 0x607D;     /* ~D65 : y = 0,3767 */
    st->hue = 0;
    st->sat = 0;
}

unsigned light_pwm_bits(uint32_t pwm_hz)
{
    /* Horloge LEDC supposée la plus lente (XTAL 40 MHz) : jamais trop optimiste. */
    if (pwm_hz < LIGHT_PWM_HZ_MIN) pwm_hz = LIGHT_PWM_HZ_MIN;
    uint32_t ticks = 40000000u / pwm_hz;
    unsigned bits = 0;
    while (bits < LIGHT_PWM_BITS_MAX && (2u << bits) <= ticks) ++bits;
    return bits;
}

float light_cie1931(float v)
{
    if (v <= 0.0f) return 0.0f;
    if (v >= 1.0f) return 1.0f;
    float l = v * 100.0f;
    return l <= 8.0f ? l / 902.3f : powf((l + 16.0f) / 116.0f, 3.0f);
}

static float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

/* Teinte pleine luminosité, 0..1 par canal (le niveau est appliqué ensuite). */
static void color_to_rgb(const light_state_t *st, float rgb[3])
{
    if (st->mode == LIGHT_COLOR_MODE_HS) {
        float h = (float)st->hue * 6.0f / 254.0f;      /* 0..6 */
        float s = clamp01((float)st->sat / 254.0f);
        int i = (int)h;
        float f = h - (float)i;
        float p = 1.0f - s, q = 1.0f - s * f, t = 1.0f - s * (1.0f - f);
        switch (i % 6) {
        case 0: rgb[0] = 1; rgb[1] = t; rgb[2] = p; break;
        case 1: rgb[0] = q; rgb[1] = 1; rgb[2] = p; break;
        case 2: rgb[0] = p; rgb[1] = 1; rgb[2] = t; break;
        case 3: rgb[0] = p; rgb[1] = q; rgb[2] = 1; break;
        case 4: rgb[0] = t; rgb[1] = p; rgb[2] = 1; break;
        default: rgb[0] = 1; rgb[1] = p; rgb[2] = q; break;
        }
        return;
    }
    float x = (float)st->x / 65536.0f;
    float y = (float)st->y / 65536.0f;
    if (y < 0.0001f) y = 0.0001f;
    float X = x / y, Z = (1.0f - x - y) / y;            /* Y = 1 */
    float r = 3.2406f * X - 1.5372f - 0.4986f * Z;
    float g = -0.9689f * X + 1.8758f + 0.0415f * Z;
    float b = 0.0557f * X - 0.2040f + 1.0570f * Z;
    if (r < 0.0f) r = 0.0f;
    if (g < 0.0f) g = 0.0f;
    if (b < 0.0f) b = 0.0f;
    float m = r > g ? r : g;
    if (b > m) m = b;
    if (m <= 0.0f) { r = g = b = 1.0f; m = 1.0f; }
    rgb[0] = r / m; rgb[1] = g / m; rgb[2] = b / m;
}

static uint32_t to_duty(float v, uint32_t max_duty)
{
    float d = clamp01(v) * (float)max_duty + 0.5f;
    return d >= (float)max_duty ? max_duty : (uint32_t)d;
}

void light_compute_rgbw(const light_state_t *st, const light_cfg_t *cfg,
                        uint32_t max_duty, light_duty_t *out)
{
    out->r = out->g = out->b = out->w = 0;
    if (!st->on || st->level == 0) return;

    float rgb[3];
    color_to_rgb(st, rgb);
    float lvl = (float)st->level / (float)LIGHT_LEVEL_MAX;
    float w = 0.0f;
    if (cfg->rgbw) {
        w = rgb[0] < rgb[1] ? rgb[0] : rgb[1];
        if (rgb[2] < w) w = rgb[2];
        for (int i = 0; i < 3; ++i) rgb[i] -= w;
    }
    float d[4] = {
        light_cie1931(rgb[0] * lvl), light_cie1931(rgb[1] * lvl),
        light_cie1931(rgb[2] * lvl), light_cie1931(w * lvl),
    };
    /* Plafond de puissance : somme R+G+B <= 3,0 x cap, proportions conservées. */
    float cap = 3.0f * (float)cfg->cap_pct / 100.0f;
    float sum = d[0] + d[1] + d[2];
    if (sum > cap) {
        float k = cap / sum;
        d[0] *= k; d[1] *= k; d[2] *= k;
    }
    out->r = to_duty(d[0], max_duty);
    out->g = to_duty(d[1], max_duty);
    out->b = to_duty(d[2], max_duty);
    out->w = to_duty(d[3], max_duty);
}

uint32_t light_compute_dimmable(bool on, uint8_t level, uint32_t max_duty)
{
    if (!on || level == 0) return 0;
    if (level > LIGHT_LEVEL_MAX) level = LIGHT_LEVEL_MAX;
    uint32_t d = to_duty(light_cie1931((float)level / (float)LIGHT_LEVEL_MAX), max_duty);
    return d ? d : 1;   /* niveau 1 : jamais éteint */
}
