#include "DesfireReader.hpp"
#include "JsonGuard.hpp"

#include <cassert>

static void checkJson(const std::vector<uint8_t>& bytes, DesfireReader::DataFormat format,
                      const std::string& expected) {
  const auto text = DesfireReader::formatFileData(bytes, static_cast<uint8_t>(format));
  assert(text);
  auto payload = JsonBuilder::object();
  payload.addString("data", *text);
  const auto json = payload.toStringUnformatted();
  auto parsed = parse_json(json);
  assert(parsed);
  const char* value = cJSON_GetStringValue(cJSON_GetObjectItem(parsed->get(), "data"));
  assert(value && std::string(value) == expected);
}

int main() {
  checkJson({'J', 0xF6, 'r', 'g', '"', '\\', '\n', 0, 'X'},
            DesfireReader::DataFormat::Latin1, "J\xC3\xB6rg\"\\\n");
  checkJson({'J', 0xC3, 0xB6, 'r', 'g', 0, 'X'},
            DesfireReader::DataFormat::Utf8, "J\xC3\xB6rg");
  checkJson({'A', 0, 'B'}, DesfireReader::DataFormat::HexUntilZero, "41");
  checkJson({'A', 0, 'B'}, DesfireReader::DataFormat::HexAll, "410042");
}
