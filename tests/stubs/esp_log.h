#pragma once

#include <cstdio>
#include <cstdint>

#define ESP_LOGE(tag, format, ...) std::fprintf(stderr, "%s: " format "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW ESP_LOGE
#define ESP_LOGI ESP_LOGE
#define ESP_LOGD ESP_LOGE

constexpr int ESP_LOG_DEBUG = 4;
inline void ESP_LOG_BUFFER_HEX_LEVEL(const char*, const void* data, size_t size, int) {
  const auto* bytes = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < size; ++i) std::fprintf(stderr, "%02X ", bytes[i]);
  if (size) std::fprintf(stderr, "\n");
}
