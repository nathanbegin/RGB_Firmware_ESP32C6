#include "driver/ledc.h"
#include "esp_log.h"
#include "board_config.h"
#include "pwm_driver.h"

static const char *TAG = "PWM";

enum { CH_R, CH_G, CH_B, CH_W, CH_P, CH_COUNT };
static const int s_gpio[CH_COUNT] = {PWM_GPIO_R, PWM_GPIO_G, PWM_GPIO_B, PWM_GPIO_W, PWM_GPIO_P};
static uint32_t s_max_duty;

static void set(int ch, uint32_t duty)
{
    if (duty > s_max_duty) duty = s_max_duty;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)ch, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)ch);
}

esp_err_t pwm_init(uint32_t pwm_hz)
{
    unsigned bits = light_pwm_bits(pwm_hz);
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = (ledc_timer_bit_t)bits,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = pwm_hz,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer);
    if (err != ESP_OK) return err;
    s_max_duty = (1u << bits) - 1u;
    for (int ch = 0; ch < CH_COUNT; ++ch) {
        ledc_channel_config_t c = {
            .gpio_num = s_gpio[ch],
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = (ledc_channel_t)ch,
            .timer_sel = LEDC_TIMER_0,
            .duty = 0,
            .hpoint = 0,
        };
        err = ledc_channel_config(&c);
        if (err != ESP_OK) return err;
    }
    ESP_LOGI(TAG, "LEDC %lu Hz, %u bits (duty max %lu)", (unsigned long)pwm_hz, bits,
             (unsigned long)s_max_duty);
    return ESP_OK;
}

uint32_t pwm_max_duty(void) { return s_max_duty; }

void pwm_set_rgbw(const light_duty_t *d)
{
    set(CH_R, d->r);
    set(CH_G, d->g);
    set(CH_B, d->b);
    set(CH_W, d->w);
}

void pwm_set_washer(uint32_t duty) { set(CH_P, duty); }
