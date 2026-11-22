#include "app_event_loop.hpp"
#include "eventStructs.hpp"

#include <array>
#include <cassert>
#include <cstring>
#include <span>
#include <system_error>
#include <vector>

static std::vector<uint8_t> posted;
static esp_event_handler_t handler = nullptr;
static void* handlerArg = nullptr;
static esp_err_t registerResult = ESP_OK;
static esp_err_t postResult = ESP_OK;

esp_err_t esp_event_handler_instance_register(esp_event_base_t, int32_t, esp_event_handler_t callback,
                                              void* arg, esp_event_handler_instance_t* instance) {
  if (registerResult != ESP_OK) return registerResult;
  handler = callback;
  handlerArg = arg;
  *instance = arg;
  return ESP_OK;
}
esp_err_t esp_event_handler_instance_unregister(esp_event_base_t, int32_t, esp_event_handler_instance_t) {
  handler = nullptr;
  handlerArg = nullptr;
  return ESP_OK;
}
esp_err_t esp_event_post(esp_event_base_t, int32_t, const void* data, size_t size, uint32_t) {
  if (postResult != ESP_OK) return postResult;
  const auto* bytes = static_cast<const uint8_t*>(data);
  posted.assign(bytes, bytes + size);
  return ESP_OK;
}

int main() {
  EventTagTap tag;
  tag.uid = {4, 17, 0, 51, 68, 85, 102};
  tag.atqa = {3, 4};
  tag.sak = 0x20;
  tag.verified = true;
  tag.hasFileData = true;
  tag.fileData.resize(1024);
  for (size_t i = 0; i < tag.fileData.size(); ++i) tag.fileData[i] = static_cast<uint8_t>(i);
  tag.propertyName = std::string(64, 'x');
  tag.cardType = "Type4ATag";
  tag.tagType = "mifare_plus_sx_ev2_sl3_4k";

  std::vector<uint8_t> tagBytes;
  alpaca::serialize(tag, tagBytes);
  NfcEvent source{.type = TAG_TAP, .data = tagBytes};
  std::vector<uint8_t> eventBytes;
  alpaca::serialize(source, eventBytes);
  assert(eventBytes.size() > 256 && eventBytes.size() <= 1536);
  std::vector<uint8_t> delivered;
  auto subscription = AppEventLoop::subscribe("NFC", NFC_TAP_EVENT, [&](const uint8_t* data, size_t size) {
    delivered.assign(data, data + size);
  });
  assert(subscription.is_valid() && handler);
  assert(AppEventLoop::publish("NFC", NFC_TAP_EVENT, eventBytes.data(), eventBytes.size()) == ESP_OK);
  handler(handlerArg, "NFC", NFC_TAP_EVENT, posted.data());
  assert(delivered == eventBytes);

  uint16_t length = 0;
  std::memcpy(&length, posted.data(), sizeof(length));
  assert(length == eventBytes.size());
  assert(posted.size() == sizeof(length) + eventBytes.size());
  std::error_code error;
  const std::span<const uint8_t> payload(posted.data() + sizeof(length), length);
  auto event = alpaca::deserialize<NfcEvent>(payload, error);
  assert(!error && event.type == TAG_TAP);
  auto received = alpaca::deserialize<EventTagTap>(event.data, error);
  assert(!error && received.uid == tag.uid && received.fileData == tag.fileData);
  assert(received.cardType == tag.cardType && received.tagType == tag.tagType);
  assert(received.verified && received.propertyName == tag.propertyName);

  const std::vector<uint8_t> maximum(1536, 0x5A);
  assert(AppEventLoop::publish("NFC", NFC_TAP_EVENT, maximum.data(), maximum.size()) == ESP_OK);
  assert(posted.size() == sizeof(length) + maximum.size());
  const auto lastPost = posted;
  const std::vector<uint8_t> oversized(1537, 0x5A);
  assert(AppEventLoop::publish("NFC", NFC_TAP_EVENT, oversized.data(), oversized.size()) == ESP_ERR_INVALID_SIZE);
  assert(posted == lastPost);
  assert(AppEventLoop::publish("NFC", NFC_TAP_EVENT, nullptr, 1) == ESP_ERR_INVALID_ARG);
  assert(posted == lastPost);
  postResult = -1;
  assert(AppEventLoop::publish("NFC", NFC_TAP_EVENT, maximum.data(), maximum.size()) == -1);
  assert(posted == lastPost);
  postResult = ESP_OK;
  assert(AppEventLoop::publish("NFC", NFC_TAP_EVENT, nullptr, 0) == ESP_OK);
  assert(posted.size() == sizeof(length));
  subscription.reset();
  assert(!handler);
  registerResult = -1;
  auto failedSubscription = AppEventLoop::subscribe("NFC", NFC_TAP_EVENT, [](const uint8_t*, size_t) {});
  assert(!failedSubscription.is_valid());
}
