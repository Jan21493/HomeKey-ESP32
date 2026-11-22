#include "DesfireReader.hpp"

#include <algorithm>

namespace {
bool isValidUtf8(std::vector<uint8_t>::const_iterator begin, std::vector<uint8_t>::const_iterator end) {
  while (begin != end) {
    const uint8_t lead = *begin++;
    if (lead < 0x80) continue;

    size_t continuationCount;
    if (lead >= 0xC2 && lead <= 0xDF) continuationCount = 1;
    else if (lead >= 0xE0 && lead <= 0xEF) continuationCount = 2;
    else if (lead >= 0xF0 && lead <= 0xF4) continuationCount = 3;
    else return false;
    if (static_cast<size_t>(end - begin) < continuationCount) return false;

    const uint8_t first = *begin;
    if ((lead == 0xE0 && first < 0xA0) || (lead == 0xED && first > 0x9F) ||
        (lead == 0xF0 && first < 0x90) || (lead == 0xF4 && first > 0x8F)) return false;
    while (continuationCount--) {
      if ((*begin++ & 0xC0) != 0x80) return false;
    }
  }
  return true;
}
}  // namespace

bool DesfireReader::isValidDataFormat(uint8_t format) {
  return format <= static_cast<uint8_t>(DataFormat::HexAll);
}

std::optional<std::string> DesfireReader::formatFileData(const std::vector<uint8_t>& data, uint8_t format) {
  if (!isValidDataFormat(format)) return std::nullopt;
  const auto mode = static_cast<DataFormat>(format);
  const auto end = mode == DataFormat::HexAll ? data.end() : std::find(data.begin(), data.end(), uint8_t{0});
  if (mode == DataFormat::Utf8) {
    if (!isValidUtf8(data.begin(), end)) return std::nullopt;
    return std::string(data.begin(), end);
  }
  if (mode == DataFormat::Latin1) {
    std::string text;
    text.reserve(static_cast<size_t>(end - data.begin()) * 2);
    for (auto it = data.begin(); it != end; ++it) {
      if (*it < 0x80) {
        text.push_back(static_cast<char>(*it));
      } else {
        text.push_back(static_cast<char>(0xC0 | (*it >> 6)));
        text.push_back(static_cast<char>(0x80 | (*it & 0x3F)));
      }
    }
    return text;
  }
  static constexpr char hex[] = "0123456789ABCDEF";
  std::string text;
  text.reserve(static_cast<size_t>(end - data.begin()) * 2);
  for (auto it = data.begin(); it != end; ++it) {
    text.push_back(hex[*it >> 4]);
    text.push_back(hex[*it & 0x0F]);
  }
  return text;
}
