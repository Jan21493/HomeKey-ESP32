#include "DesfireReader.hpp"

#include <cassert>

using Format = DesfireReader::DataFormat;
using Bytes = std::vector<uint8_t>;

static std::optional<std::string> format(const Bytes& data, Format mode) {
  return DesfireReader::formatFileData(data, static_cast<uint8_t>(mode));
}

int main() {
  const Bytes latin1 = {'J', 0xF6, 'r', 'g', ' ', 0xFC, 0xDF, 0, 'X', 0};
  const Bytes utf8 = {'J', 0xC3, 0xB6, 'r', 'g', ' ', 0xC3, 0xBC, 0xC3, 0x9F, 0, 'X'};
  const std::string text = "J\xC3\xB6rg \xC3\xBC\xC3\x9F";
  assert(format(latin1, Format::Latin1) == text);
  assert(format(utf8, Format::Utf8) == text);
  assert(format(latin1, Format::HexUntilZero) == "4AF6726720FCDF");
  assert(format(latin1, Format::HexAll) == "4AF6726720FCDF005800");
  assert(format({0, 0xAB}, Format::Latin1) == "");
  assert(format({0, 0xAB}, Format::Utf8) == "");
  assert(format({0, 0xAB}, Format::HexUntilZero) == "");
  assert(format({0, 0xAB}, Format::HexAll) == "00AB");
  assert(format({}, Format::HexAll) == "");
  assert(format({'A', 0, 0xFF}, Format::Utf8) == "A");
  assert(format({0x80, 0xFF}, Format::Latin1) == "\xC2\x80\xC3\xBF");
  assert(format({0xC2, 0x80, 0xE0, 0xA0, 0x80, 0xED, 0x9F, 0xBF,
                 0xF0, 0x90, 0x80, 0x80, 0xF4, 0x8F, 0xBF, 0xBF}, Format::Utf8));
  for (const Bytes& invalid : std::vector<Bytes>{
           {0x80}, {0xC0, 0x80}, {0xC1, 0xBF}, {0xC2}, {0xC2, 'A'},
           {0xE0, 0x9F, 0xBF}, {0xED, 0xA0, 0x80}, {0xE2, 0x82},
           {0xF0, 0x8F, 0xBF, 0xBF}, {0xF4, 0x90, 0x80, 0x80},
           {0xF5, 0x80, 0x80, 0x80}, {0xFF}, {0xC3, 0, 0xB6}}) {
    assert(!format(invalid, Format::Utf8));
  }
  for (uint8_t mode = 0; mode <= 3; ++mode) assert(DesfireReader::isValidDataFormat(mode));
  assert(!DesfireReader::isValidDataFormat(4));
  assert(!DesfireReader::formatFileData({'A'}, 4));
}
