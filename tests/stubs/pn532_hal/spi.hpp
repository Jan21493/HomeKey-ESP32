#pragma once

#include "pn532_cxx/transport.hpp"
#include "soc/gpio_num.h"
#include <vector>

namespace pn532 {
class SpiTransport : public Transport {
public:
  SpiTransport(gpio_num_t, gpio_num_t, gpio_num_t, gpio_num_t, gpio_num_t);
  void swReset() override;
  void abort() override;
  Transaction begin() override;

protected:
  Status writeChunk(span<const uint8_t> data) override;
  bool waitReady(uint32_t timeoutMs) override;
  Status prepareRead() override;
  Status readChunk(span<uint8_t> buffer) override;
  void endTransaction() override;

private:
  std::vector<uint8_t> input;
  size_t offset = 0;
  bool timeout = false;
};
} // namespace pn532
