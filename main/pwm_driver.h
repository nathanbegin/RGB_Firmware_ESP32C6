/*
 * pwm_driver.h — 5 canaux LEDC sur un même temporisateur (R, G, B, W, P).
 * Les sorties démarrent à 0 : les MOSFET restent bloqués tant que le
 * firmware n'a pas appliqué un état.
 */
#pragma once

#include "esp_err.h"
#include "light_engine.h"

esp_err_t pwm_init(uint32_t pwm_hz);
uint32_t pwm_max_duty(void);
void pwm_set_rgbw(const light_duty_t *d);
void pwm_set_washer(uint32_t duty);
