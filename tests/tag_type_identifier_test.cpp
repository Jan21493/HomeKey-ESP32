#include "TagTypeIdentifier.hpp"

#include <cassert>
#include <string_view>

using Bytes = std::vector<uint8_t>;

static void check(const TagTypeIdentification& result, std::string_view card, std::string_view tag) {
  assert(result.cardType == card);
  assert(result.tagType == tag);
}

int main() {
  size_t calls = 0;
  const TagTypeIdentifier::Exchange fail = [&](const Bytes&, Bytes&) {
    ++calls;
    return false;
  };
  check(TagTypeIdentifier::identify(4, {0x00, 0x04}, 0x09, fail, fail), "Type2Tag", "mifare_mini");
  check(TagTypeIdentifier::identify(4, {0x04, 0x00}, 0x08, fail, fail), "Type2Tag", "mifare_classic_1k");
  check(TagTypeIdentifier::identify(7, {0x00, 0x02}, 0x19, fail, fail), "Type2Tag", "mifare_classic_2k");
  check(TagTypeIdentifier::identify(7, {0x02, 0x00}, 0x18, fail, fail), "Type2Tag", "mifare_classic_4k");
  check(TagTypeIdentifier::identify(7, {0x03, 0x44}, 0x10, fail, fail), "Type4ATag", "mifare_plus_sl2_2k");
  check(TagTypeIdentifier::identify(4, {0x44, 0x03}, 0x11, fail, fail), "Type4ATag", "mifare_plus_sl2_4k");
  check(TagTypeIdentifier::identify(7, {0x00, 0x04}, 0x08, fail, fail), "Unknown", "unknown");
  check(TagTypeIdentifier::identify(4, {0x00, 0x04}, 0x20, fail, fail), "Type4ATag", "unknown");
  check(TagTypeIdentifier::identify(7, {0x44, 0x00}, 0x20, fail, fail), "Type4ATag", "unknown");
  assert(calls == 0);

  struct Type2Case {
    uint8_t type, subtype, major, size;
    const char* name;
  };
  for (const auto& test : std::vector<Type2Case>{
           {0x03, 0x01, 0x01, 0x0B, "mifare_ultralight_ev1_48"},
           {0x03, 0x01, 0x01, 0x0E, "mifare_ultralight_ev1_128"},
           {0x03, 0x01, 0x04, 0x0B, "mifare_ultralight_aes_48"},
           {0x03, 0x01, 0x04, 0x0E, "mifare_ultralight_aes_128"},
           {0x04, 0x01, 0x01, 0x0B, "ntag_210"},
           {0x04, 0x01, 0x01, 0x0E, "ntag_212"},
           {0x04, 0x02, 0x01, 0x0F, "ntag_213"},
           {0x04, 0x02, 0x01, 0x11, "ntag_215"},
           {0x04, 0x02, 0x01, 0x13, "ntag_216"},
           {0x04, 0x05, 0x01, 0x13, "ntag_i2c_plus_1k"},
           {0x04, 0x05, 0x01, 0x15, "ntag_i2c_plus_2k"}}) {
    const auto exchange = [&](const Bytes& request, Bytes& response) {
      assert(request == Bytes{0x60});
      response = {0, 0x04, test.type, test.subtype, test.major, 0, test.size, 0x03};
      return true;
    };
    const auto result = TagTypeIdentifier::identify(7, {0x44, 0x00}, 0, exchange, fail);
    check(result, "Type2Tag", test.name);
    assert(result.versionAttempted && result.versionRead);
  }
  for (const Bytes& bad : std::vector<Bytes>{
           {}, {0, 4, 4, 2, 1, 0, 0x11}, {1, 4, 4, 2, 1, 0, 0x11, 3}}) {
    const auto exchange = [&](const Bytes&, Bytes& response) { response = bad; return true; };
    const auto result = TagTypeIdentifier::identify(7, {0, 0x44}, 0, exchange, fail);
    check(result, "Type2Tag", "unknown");
    assert(result.versionAttempted && !result.versionRead);
  }
  for (const Bytes& unknown : std::vector<Bytes>{
           {0, 5, 4, 2, 1, 0, 0x11, 3}, {0, 4, 4, 2, 1, 0, 0x11, 5},
           {0, 4, 4, 2, 1, 0, 0x12, 3}}) {
    const auto exchange = [&](const Bytes&, Bytes& response) { response = unknown; return true; };
    const auto result = TagTypeIdentifier::identify(7, {0, 0x44}, 0, exchange, fail);
    check(result, "Type2Tag", "unknown");
    assert(result.versionAttempted && result.versionRead);
  }

  struct IsoCase {
    uint8_t type, major, minor, size;
    const char* name;
  };
  for (const auto& test : std::vector<IsoCase>{
           {0x01, 0x01, 0, 0x18, "mifare_desfire_ev1"},
           {0x01, 0x02, 0, 0x1E, "mifare_desfire_ev2"},
           {0x01, 0x03, 0, 0x1A, "mifare_desfire_ev3"},
           {0x01, 0xA0, 0, 0x16, "mifare_duox_2k"},
           {0x01, 0xA0, 0, 0x18, "mifare_duox_4k"},
           {0x01, 0xA0, 0, 0x1A, "mifare_duox_8k"},
           {0x01, 0xA0, 0, 0x1C, "mifare_duox_16k"},
           {0x02, 0x11, 0, 0x16, "mifare_plus_sx_ev1_sl3_2k"},
           {0x02, 0x11, 0, 0x18, "mifare_plus_sx_ev1_sl3_4k"},
           {0x02, 0x22, 0, 0x16, "mifare_plus_sx_ev2_sl3_2k"},
           {0x02, 0x22, 0, 0x18, "mifare_plus_sx_ev2_sl3_4k"},
           {0x08, 0x01, 0, 0x13, "mifare_desfire_light"},
           {0x04, 0x30, 0, 0x11, "ntag_424_dna"},
           {0x07, 0x01, 0, 0x11, "ntag_5_switch"},
           {0x07, 0x01, 0, 0x13, "ntag_5_link"}}) {
    size_t frame = 0;
    const auto exchange = [&](const Bytes& request, Bytes& response) {
      if (frame == 0) {
        assert((request == Bytes{0x90, 0x60, 0, 0, 0}));
        response = {0x04, test.type, 1, test.major, test.minor, test.size, 0x05, 0x91, 0xAF};
      } else {
        assert((request == Bytes{0x90, 0xAF, 0, 0, 0}));
        response.assign(frame == 1 ? 7 : 14, 0);
        response.push_back(0x91);
        response.push_back(frame == 1 ? 0xAF : 0x00);
      }
      ++frame;
      return true;
    };
    const auto result = TagTypeIdentifier::identify(4, {0x03, 0x04}, 0x20, fail, exchange);
    check(result, "Type4ATag", test.name);
    assert(result.versionAttempted && result.versionRead && frame == 3);
  }

  size_t nativeFrame = 0;
  const auto nativePlus = [&](const Bytes& request, Bytes& response) {
    if (nativeFrame == 0) {
      assert((request == Bytes{0x90, 0x60, 0, 0, 0}));
      response = {0x6E, 0x00};
    } else if (nativeFrame == 1) {
      assert(request == Bytes{0x60});
      response = {0xAF, 0x04, 0x02, 1, 0x11, 0, 0x16, 0x05};
    } else {
      assert(request == Bytes{0xAF});
      response.assign(nativeFrame == 2 ? 8 : 15, 0);
      response[0] = nativeFrame == 2 ? 0xAF : 0x90;
    }
    ++nativeFrame;
    return true;
  };
  check(TagTypeIdentifier::identify(7, {0x44, 0x03}, 0x20, fail, nativePlus),
        "Type4ATag", "mifare_plus_sx_ev1_sl3_2k");
  assert(nativeFrame == 4);

  for (const Bytes& unexpected : std::vector<Bytes>{
           {0x90, 0x00}, {0x01, 0x90, 0x00}, {0x6A, 0x82}, {0x91, 0xAE}, {0x06}}) {
    size_t attempts = 0;
    const auto exchange = [&](const Bytes&, Bytes& response) {
      ++attempts;
      response = unexpected;
      return true;
    };
    const auto result = TagTypeIdentifier::identify(4, {0x03, 0x04}, 0x20, fail, exchange);
    check(result, "Type4ATag", "unknown");
    assert(result.versionAttempted && !result.versionRead && attempts == 1);
  }

  size_t endlessFrames = 0;
  const auto endless = [&](const Bytes&, Bytes& response) {
    response = {4, 1, 1, 1, 0, 0x18, 5, 0x91, 0xAF};
    ++endlessFrames;
    return true;
  };
  const auto failed = TagTypeIdentifier::identify(4, {0x03, 0x04}, 0x20, fail, endless);
  check(failed, "Type4ATag", "unknown");
  assert(failed.versionAttempted && !failed.versionRead && endlessFrames == 3);

  size_t truncatedFrames = 0;
  const auto truncated = [&](const Bytes&, Bytes& response) {
    response = {4, 1, 1, 1, 0, 0x18, 5, 0x91, truncatedFrames++ == 0 ? uint8_t{0xAF} : uint8_t{0x00}};
    return true;
  };
  const auto incomplete = TagTypeIdentifier::identify(4, {0x03, 0x04}, 0x20, fail, truncated);
  check(incomplete, "Type4ATag", "unknown");
  assert(!incomplete.versionRead && truncatedFrames == 2);
}
