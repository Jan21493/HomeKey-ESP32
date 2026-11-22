#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

struct TagTypeIdentification {
  const char* cardType = "Unknown";
  const char* tagType = "unknown";
  bool versionAttempted = false;
  bool versionRead = false;
};

class TagTypeIdentifier {
public:
  using Exchange = std::function<bool(const std::vector<uint8_t>&, std::vector<uint8_t>&)>;

  static TagTypeIdentification identify(size_t uidLength, const std::array<uint8_t, 2>& atqa, uint8_t sak,
                                        const Exchange& type2Exchange, const Exchange& isoDepExchange);
};
