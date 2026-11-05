#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "light_engine.h"

#define MAXD 8191u
static light_cfg_t cfg(unsigned cap, bool rgbw)
{
    light_cfg_t c; light_cfg_defaults(&c); c.cap_pct = (uint8_t)cap; c.rgbw = rgbw; return c;
}
static light_state_t white_xy(uint8_t level)
{
    light_state_t s; light_state_defaults(&s); s.on = true; s.level = level;
    s.x = 20493; s.y = 21561;   /* D65 : x = 0,3127  y = 0,3290 */
    return s;
}
static int near(uint32_t a, uint32_t b, uint32_t tol) { return (a > b ? a - b : b - a) <= tol; }

int main(void)
{
    light_duty_t d;

    /* Éteint, ou niveau 0 : tout à zéro. */
    light_state_t s = white_xy(254); s.on = false;
    light_cfg_t c = cfg(100, false);
    light_compute_rgbw(&s, &c, MAXD, &d);
    assert(!d.r && !d.g && !d.b && !d.w);
    s.on = true; s.level = 0;
    light_compute_rgbw(&s, &c, MAXD, &d);
    assert(!d.r && !d.g && !d.b && !d.w);

    /* Blanc D65 plein, sans plafond : R ~ G ~ B ~ max, W = 0 (ruban RGB). */
    s = white_xy(254);
    light_compute_rgbw(&s, &c, MAXD, &d);
    assert(near(d.r, MAXD, 150) && near(d.g, MAXD, 150) && near(d.b, MAXD, 150) && d.w == 0);

    /* Plafond par défaut 75 % : somme R+G+B <= 2,25 x max, couleur conservée. */
    c = cfg(75, false);
    light_compute_rgbw(&s, &c, MAXD, &d);
    assert((d.r + d.g + d.b) <= (uint32_t)(2.25f * MAXD) + 3);
    assert(near(d.r, d.g, 250) && near(d.g, d.b, 250));
    assert(d.r > MAXD / 2);

    /* Rouge pur en HS : seul R, aucun plafond actif (somme = 1). */
    s = white_xy(254); s.mode = LIGHT_COLOR_MODE_HS; s.hue = 0; s.sat = 254;
    light_compute_rgbw(&s, &c, MAXD, &d);
    assert(d.r == MAXD && d.g == 0 && d.b == 0 && d.w == 0);
    /* Vert (hue 85 ~ 120°) et bleu (hue 170 ~ 240°). */
    s.hue = 85; light_compute_rgbw(&s, &c, MAXD, &d); assert(d.g > d.r && d.g > d.b);
    s.hue = 170; light_compute_rgbw(&s, &c, MAXD, &d); assert(d.b > d.r && d.b > d.g);
    /* Saturation 0 en HS : blanc. */
    s.sat = 0; light_compute_rgbw(&s, &c, MAXD, &d); assert(d.r == d.g && d.g == d.b && d.r > 0);

    /* Option RGBW : le blanc passe sur W, R/G/B retombent à ~0. */
    c = cfg(100, true);
    s = white_xy(254);
    light_compute_rgbw(&s, &c, MAXD, &d);
    assert(d.w > MAXD * 9 / 10);
    assert(d.r < MAXD / 10 && d.g < MAXD / 10 && d.b < MAXD / 10);
    /* Rouge pur avec RGBW : W = min(R,G,B) = 0. */
    s.mode = LIGHT_COLOR_MODE_HS; s.hue = 0; s.sat = 254;
    light_compute_rgbw(&s, &c, MAXD, &d);
    assert(d.w == 0 && d.r == MAXD);

    /* Le plafond ne s'applique pas à W. */
    c = cfg(10, true);
    s = white_xy(254);
    light_compute_rgbw(&s, &c, MAXD, &d);
    assert(d.w > MAXD * 9 / 10);

    /* Monotonie du niveau (rondelles et RGB) et niveau 1 jamais éteint. */
    uint32_t prev = 0;
    for (unsigned lvl = 1; lvl <= LIGHT_LEVEL_MAX; ++lvl) {
        uint32_t v = light_compute_dimmable(true, (uint8_t)lvl, MAXD);
        assert(v >= prev && v > 0);
        prev = v;
    }
    assert(light_compute_dimmable(true, 254, MAXD) == MAXD);
    assert(light_compute_dimmable(false, 254, MAXD) == 0);
    assert(light_compute_dimmable(true, 0, MAXD) == 0);
    c = cfg(100, false); prev = 0;
    for (unsigned lvl = 1; lvl <= LIGHT_LEVEL_MAX; ++lvl) {
        s = white_xy((uint8_t)lvl);
        light_compute_rgbw(&s, &c, MAXD, &d);
        assert(d.g >= prev); prev = d.g;
    }

    /* Courbe CIE 1931 : bornes et rupture à L = 8. */
    assert(light_cie1931(0.0f) == 0.0f && light_cie1931(1.0f) == 1.0f);
    assert(light_cie1931(-1.0f) == 0.0f && light_cie1931(2.0f) == 1.0f);
    assert(fabsf(light_cie1931(0.5f) - 0.184187f) < 0.001f);

    /* Résolution LEDC selon la fréquence. */
    assert(light_pwm_bits(300) == 13 && light_pwm_bits(2000) == 13);
    assert(light_pwm_bits(5000) == 12 && light_pwm_bits(20000) == 10);
    for (unsigned hz = 300; hz <= 20000; hz += 100) {
        unsigned b = light_pwm_bits(hz);
        assert(b >= 10 && b <= 13);
        assert((1u << b) <= 40000000u / hz);   /* jamais plus que l'horloge ne permet */
    }

    /* Réglages hors plage ramenés dans la plage. */
    light_cfg_t bad = {.pwm_hz = 5, .cap_pct = 0, .rgbw = true};
    light_cfg_sanitize(&bad);
    assert(bad.pwm_hz == LIGHT_PWM_HZ_MIN && bad.cap_pct == LIGHT_CAP_PCT_MIN && bad.rgbw);
    bad.pwm_hz = 65000; bad.cap_pct = 250;
    light_cfg_sanitize(&bad);
    assert(bad.pwm_hz == LIGHT_PWM_HZ_MAX && bad.cap_pct == LIGHT_CAP_PCT_MAX);
    light_cfg_t def; light_cfg_defaults(&def);
    assert(def.pwm_hz == 2000 && def.cap_pct == 75 && !def.rgbw);

    puts("PASS: moteur de lumière (couleur, plafond, RGBW, gamma, résolution PWM)");
    return 0;
}
