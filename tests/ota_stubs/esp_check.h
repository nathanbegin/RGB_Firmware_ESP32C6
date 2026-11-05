#pragma once
#include "esp_zigbee_core.h"
void test_log(const char *, const char *, ...);
#define ESP_LOGI(...) test_log(__VA_ARGS__)
#define ESP_LOGW(...) test_log(__VA_ARGS__)
#define ESP_RETURN_ON_ERROR(expr, tag, msg) do {esp_err_t e = (expr); if (e) return e;} while (0)
const char *esp_err_to_name(esp_err_t);
