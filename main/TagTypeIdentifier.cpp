#include "TagTypeIdentifier.hpp"

#include <algorithm>
#include <initializer_list>
#include <optional>
#include <utility>

namespace {
using Version = std::array<uint8_t, 7>;
using Exchange = TagTypeIdentifier::Exchange;

bool matchesAtqa(const std::array<uint8_t, 2>& atqa, uint16_t expected) {
  const uint8_t high = static_cast<uint8_t>(expected >> 8);
  const uint8_t low = static_cast<uint8_t>(expected);
  return (atqa[0] == high && atqa[1] == low) || (atqa[0] == low && atqa[1] == high);
}

bool isSize(uint8_t value, std::initializer_list<uint8_t> sizes) {
  return std::find(sizes.begin(), sizes.end(), value) != sizes.end();
}

const char* identifyVersion(const Version& v, uint8_t expectedProtocol) {
  if (v[0] != 0x04 || v[6] != expectedProtocol) return "unknown";
  const auto type = v[1];
  const auto subtype = v[2];
  const auto major = v[3];
  const auto minor = v[4];
  const auto size = v[5];

  if (expectedProtocol == 0x03) {
    if (type == 0x03 && subtype == 0x01) {
      if (major == 0x01 && size == 0x0B) return "mifare_ultralight_ev1_48";
      if (major == 0x01 && size == 0x0E) return "mifare_ultralight_ev1_128";
      if (major == 0x04 && size == 0x0B) return "mifare_ultralight_aes_48";
      if (major == 0x04 && size == 0x0E) return "mifare_ultralight_aes_128";
    }
    if (type == 0x04) {
      if (subtype == 0x01 && size == 0x0B) return "ntag_210";
      if (subtype == 0x01 && size == 0x0E) return "ntag_212";
      if (subtype == 0x02 && size == 0x0F) return "ntag_213";
      if (subtype == 0x02 && size == 0x11) return "ntag_215";
      if (subtype == 0x02 && size == 0x13) return "ntag_216";
      if (subtype == 0x05 && size == 0x13) return "ntag_i2c_plus_1k";
      if (subtype == 0x05 && size == 0x15) return "ntag_i2c_plus_2k";
    }
  } else if (expectedProtocol == 0x05) {
    if (type == 0x01) {
      if (major == 0xA0) {
        if (size == 0x16) return "mifare_duox_2k";
        if (size == 0x18) return "mifare_duox_4k";
        if (size == 0x1A) return "mifare_duox_8k";
        if (size == 0x1C) return "mifare_duox_16k";
      }
      if (major == 0x01 && isSize(size, {0x10, 0x16, 0x18, 0x1A})) return "mifare_desfire_ev1";
      if (major == 0x02 && isSize(size, {0x16, 0x18, 0x1A, 0x1C, 0x1E})) return "mifare_desfire_ev2";
      if (major == 0x03 && isSize(size, {0x16, 0x18, 0x1A})) return "mifare_desfire_ev3";
    }
    if (type == 0x02) {
      if (major == 0x11 && size == 0x16) return "mifare_plus_sx_ev1_sl3_2k";
      if (major == 0x11 && size == 0x18) return "mifare_plus_sx_ev1_sl3_4k";
      if (major == 0x22 && size == 0x16) return "mifare_plus_sx_ev2_sl3_2k";
      if (major == 0x22 && size == 0x18) return "mifare_plus_sx_ev2_sl3_4k";
    }
    if (type == 0x08 && size == 0x13) return "mifare_desfire_light";
    if (type == 0x04 && major == 0x30 && minor == 0x00 && size == 0x11) return "ntag_424_dna";
    if (type == 0x07 && size == 0x11) return "ntag_5_switch";
    if (type == 0x07 && size == 0x13) return "ntag_5_link";
  }
  return "unknown";
}

std::optional<Version> readType2Version(const Exchange& exchange) {
  if (!exchange) return std::nullopt;
  std::vector<uint8_t> response;
  if (!exchange({0x60}, response) || response.size() != 8 || response[0] != 0x00) return std::nullopt;
  Version version;
  std::copy_n(response.begin() + 1, version.size(), version.begin());
  return version;
}

std::optional<Version> readIsoVersionFrames(const Exchange& exchange, std::vector<uint8_t> response, bool wrapped) {
  Version version{};
  size_t total = 0;
  for (size_t frame = 0; frame < 3; ++frame) {
    if (response.size() < (wrapped ? 2 : 1)) return std::nullopt;
    if (wrapped && response[response.size() - 2] != 0x91) return std::nullopt;
    const uint8_t status = wrapped ? response.back() : response.front();
    const size_t payloadSize = response.size() - (wrapped ? 2 : 1);
    total += payloadSize;
    if (total > 64) return std::nullopt;
    if (frame == 0) {
      if (payloadSize < version.size()) return std::nullopt;
      std::copy_n(response.begin() + (wrapped ? 0 : 1), version.size(), version.begin());
    }
    if (status == 0x00 || (!wrapped && status == 0x90)) {
      return total >= 28 ? std::optional<Version>{version} : std::nullopt;
    }
    if (status != 0xAF || frame == 2) return std::nullopt;
    response.clear();
    const std::vector<uint8_t> next = wrapped ? std::vector<uint8_t>{0x90, 0xAF, 0x00, 0x00, 0x00}
                                              : std::vector<uint8_t>{0xAF};
    if (!exchange(next, response)) return std::nullopt;
  }
  return std::nullopt;
}

std::optional<Version> readIsoVersion(const Exchange& exchange) {
  if (!exchange) return std::nullopt;
  std::vector<uint8_t> response;
  if (!exchange({0x90, 0x60, 0x00, 0x00, 0x00}, response) || response.empty()) return std::nullopt;
  if (response.size() >= 2 && response[response.size() - 2] == 0x91 &&
      (response.back() == 0xAF || response.back() == 0x00)) {
    return readIsoVersionFrames(exchange, std::move(response), true);
  }
  const bool unsupported = response.size() == 2 &&
      ((response[0] == 0x6D && response[1] == 0x00) ||
       (response[0] == 0x6E && response[1] == 0x00) ||
       (response[0] == 0x6A && response[1] == 0x81));
  if (!unsupported) return std::nullopt;
  response.clear();
  if (!exchange({0x60}, response)) return std::nullopt;
  return readIsoVersionFrames(exchange, std::move(response), false);
}
}  // namespace

TagTypeIdentification TagTypeIdentifier::identify(size_t uidLength, const std::array<uint8_t, 2>& atqa,
                                                   uint8_t sak, const Exchange& type2Exchange,
                                                   const Exchange& isoDepExchange) {
  TagTypeIdentification result;
  if ((uidLength == 4 && matchesAtqa(atqa, 0x0004)) ||
      (uidLength == 7 && matchesAtqa(atqa, 0x0002))) {
    const char* classic = nullptr;
    if (uidLength == 4 && sak == 0x09) classic = "mifare_mini";
    else if (sak == 0x08) classic = "mifare_classic_1k";
    else if (sak == 0x19) classic = "mifare_classic_2k";
    else if (sak == 0x18) classic = "mifare_classic_4k";
    if (classic) return {"Type2Tag", classic};
  }
  if ((uidLength == 4 || uidLength == 7) && matchesAtqa(atqa, 0x0344)) {
    if (sak == 0x10) return {"Type4ATag", "mifare_plus_sl2_2k"};
    if (sak == 0x11) return {"Type4ATag", "mifare_plus_sl2_4k"};
  }
  if (sak == 0x00 && (uidLength == 4 || uidLength == 7 || uidLength == 10)) {
    result.cardType = "Type2Tag";
    if (matchesAtqa(atqa, 0x0044) || matchesAtqa(atqa, 0x0344) ||
        (uidLength == 4 && matchesAtqa(atqa, 0x0004))) {
      result.versionAttempted = true;
      if (const auto version = readType2Version(type2Exchange)) {
        result.versionRead = true;
        result.tagType = identifyVersion(*version, 0x03);
      }
    }
  } else if ((sak & 0x20) && (uidLength == 4 || uidLength == 7 || uidLength == 10)) {
    result.cardType = "Type4ATag";
    if (!matchesAtqa(atqa, 0x0004) && !matchesAtqa(atqa, 0x0044) &&
        !matchesAtqa(atqa, 0x0048) && !matchesAtqa(atqa, 0x0008)) {
      result.versionAttempted = true;
      if (const auto version = readIsoVersion(isoDepExchange)) {
        result.versionRead = true;
        result.tagType = identifyVersion(*version, 0x05);
      }
    }
  }
  return result;
}
