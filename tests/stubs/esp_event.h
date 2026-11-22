#pragma once

#include <cstddef>
#include <cstdint>

using esp_err_t = int;
using esp_event_base_t = const char*;
using esp_event_handler_instance_t = void*;
using esp_event_handler_t = void (*)(void*, esp_event_base_t, int32_t, void*);

constexpr esp_err_t ESP_OK = 0;
constexpr esp_err_t ESP_ERR_INVALID_ARG = 0x102;
constexpr esp_err_t ESP_ERR_INVALID_SIZE = 0x104;
constexpr uint32_t portMAX_DELAY = UINT32_MAX;

#define ESP_EVENT_DECLARE_BASE(name) extern const char* name

esp_err_t esp_event_handler_instance_register(esp_event_base_t, int32_t, esp_event_handler_t, void*,
                                              esp_event_handler_instance_t*);
esp_err_t esp_event_handler_instance_unregister(esp_event_base_t, int32_t, esp_event_handler_instance_t);
esp_err_t esp_event_post(esp_event_base_t, int32_t, const void*, size_t, uint32_t);
