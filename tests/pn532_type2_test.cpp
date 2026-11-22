#include "Pn532Reader.hpp"
#include "TagTypeIdentifier.hpp"

#include <algorithm>
#include <cassert>
#include <string_view>
#include <utility>

using Bytes = std::vector<uint8_t>;

static std::vector<Bytes> commands;
static Bytes type2Response = {0x43, 0, 0, 4, 4, 2, 1, 0, 0x11, 3};
static bool type2Timeout = false;
static bool corruptChecksum = false;
static const Bytes uid = {4, 0x23, 0x9C, 0xAB, 0x40, 2, 0x89};

namespace pn532 {
SpiTransport::SpiTransport(gpio_num_t, gpio_num_t, gpio_num_t, gpio_num_t, gpio_num_t) {}
void SpiTransport::swReset() {}
void SpiTransport::abort() { input.clear(); }
Transaction SpiTransport::begin() { return Transaction(*this, true); }
void SpiTransport::endTransaction() {}
Status SpiTransport::prepareRead() { return Status::SUCCESS; }
bool SpiTransport::waitReady(uint32_t) { return !timeout; }

Status SpiTransport::readChunk(span<uint8_t> buffer) {
  if (buffer.size() > input.size() - offset) return Status::NO_SPACE;
  std::copy_n(input.begin() + offset, buffer.size(), buffer.begin());
  offset += buffer.size();
  return Status::SUCCESS;
}

Status SpiTransport::writeChunk(span<const uint8_t> data) {
  assert(data.size() >= 8 && data[5] == 0xD4);
  const Bytes command(data.begin() + 6, data.end() - 2);
  commands.push_back(command);
  Bytes response;
  timeout = false;
  bool corrupt = false;
  switch (command[0]) {
    case cmd::GetFirmwareVersion:
      response = {3, 0x32, 1, 6, 7};
      break;
    case cmd::InCommunicateThru:
      if (command == Bytes{0x42, 0x60}) {
        response = type2Response;
        timeout = type2Timeout;
        corrupt = corruptChecksum;
      } else {
        response = {0x43, 0};
      }
      break;
    case cmd::InDataExchange:
      response = {0x41, 0, 0x90, 0};
      break;
    case cmd::InListPassiveTarget:
      response = {0x4B, 1, 1, 0x44, 0, 0, 7};
      response.insert(response.end(), uid.begin(), uid.end());
      break;
    default:
      response = {static_cast<uint8_t>(command[0] + 1)};
      break;
  }
  const uint8_t length = static_cast<uint8_t>(response.size() + 1);
  input = {0, 0, 0xFF, 0, 0xFF, 0, // ACK
           0, 0, 0xFF, length, static_cast<uint8_t>(-length), 0xD5};
  uint8_t sum = 0xD5;
  for (const uint8_t byte : response) {
    input.push_back(byte);
    sum = static_cast<uint8_t>(sum + byte);
  }
  input.push_back(static_cast<uint8_t>(-sum) ^ (corrupt ? 1 : 0));
  input.push_back(0);
  offset = 0;
  return Status::SUCCESS;
}
} // namespace pn532

int main() {
  const std::array<uint8_t, 4> pins = {1, 2, 3, 4};
  const std::array<uint8_t, 18> ecp = {};
  Pn532Reader reader(pins, ecp);
  Bytes response = {0xAA};
  assert(!reader.exchangeType2({0x60}, response, 100) && response.empty());
  assert(reader.init());
  Bytes detected;
  std::array<uint8_t, 2> atqa;
  uint8_t sak;
  assert(reader.pollForTag(detected, atqa, sak, 100));
  assert(detected == uid && (atqa == std::array<uint8_t, 2>{0x44, 0}) && sak == 0);
  commands.clear();
  const auto type2Exchange = [&](const Bytes& send, Bytes& recv) {
    return reader.exchangeType2(send, recv, 100);
  };
  const auto isoExchange = [&](const Bytes& send, Bytes& recv) {
    return reader.exchangeApdu(send, recv, 100);
  };
  const auto result = TagTypeIdentifier::identify(detected.size(), atqa, sak, type2Exchange, isoExchange);
  assert(result.versionRead && std::string_view(result.cardType) == "Type2Tag");
  assert(std::string_view(result.tagType) == "ntag_215");
  assert((commands == std::vector<Bytes>{{0x42, 0x60}}));

  for (const auto& test : std::vector<std::pair<uint8_t, const char*>>{
           {0x0F, "ntag_213"}, {0x11, "ntag_215"}, {0x13, "ntag_216"}}) {
    type2Response[8] = test.first;
    const auto identified = TagTypeIdentifier::identify(7, atqa, 0, type2Exchange, isoExchange);
    assert(identified.versionRead && std::string_view(identified.tagType) == test.second);
  }

  for (const Bytes& invalid : std::vector<Bytes>{
           {}, {0x43}, {0x41, 0, 0xAA}, {0x43, 1}, {0x43, 2}, {0x43, 0x40}}) {
    type2Response = invalid;
    response = {0xAA};
    assert(!reader.exchangeType2({0x60}, response, 100) && response.empty());
  }
  type2Response = {0x43, 0, 0, 4, 4, 2, 1, 0, 0x11, 3};
  type2Timeout = true;
  assert(!reader.exchangeType2({0x60}, response, 100) && response.empty());
  type2Timeout = false;
  corruptChecksum = true;
  assert(!reader.exchangeType2({0x60}, response, 100) && response.empty());
  corruptChecksum = false;

  const size_t previousCommands = commands.size();
  assert(!reader.exchangeType2({}, response, 100));
  assert(!reader.exchangeType2(Bytes(253), response, 100));
  assert(!reader.exchangeType2({0x60}, response, 65536));
  assert(commands.size() == previousCommands);

  const Bytes select = {0, 0xA4, 4, 0, 0};
  assert(reader.exchangeApdu(select, response, 100));
  assert((response == Bytes{0x90, 0}));
  Bytes expected = {0x40, 1};
  expected.insert(expected.end(), select.begin(), select.end());
  assert(commands.back() == expected);
}
