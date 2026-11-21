#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

/**
 * Minimal MIFARE DESFire EV1 client: select application, authenticate
 * (AES 0xAA or ISO 0x1A for 2K3DES/3K3DES) and read one standard/backup
 * data file in plain, MACed or fully enciphered communication mode.
 *
 * Commands are sent ISO 7816-4 wrapped (CLA 0x90) through any ISO-DEP
 * transceive function, so it works with every supported reader.
 */
class DesfireReader {
public:
  enum class KeyType : uint8_t { AES128 = 0, TDES2K = 1, TDES3K = 2 };
  enum class CommMode : uint8_t { Plain = 0, Mac = 1, Full = 3 };

  struct Config {
    std::array<uint8_t, 3> aid{};   // little-endian, as sent on the wire
    uint8_t fileNo = 0;
    uint8_t keyNo = 0;
    KeyType keyType = KeyType::AES128;
    CommMode commMode = CommMode::Plain;
    uint16_t readLength = 0;        // 0 = whole file
    std::vector<uint8_t> key;       // 16 or 24 bytes
  };

  using Transceive = std::function<bool(const std::vector<uint8_t>& send, std::vector<uint8_t>& recv)>;

  /** Build a Config from the textual web settings. Returns nullopt if invalid. */
  static std::optional<Config> makeConfig(const std::string& aidHex, uint8_t fileNo, uint8_t keyNo,
                                          uint8_t keyType, uint8_t commMode, uint16_t readLength,
                                          const std::string& keyHex);

  /** DESFire (EV1/EV2/EV3) answers anticollision with SAK bit 0x20 (ISO 14443-4). */
  static bool looksLikeDesfire(const std::array<uint8_t, 2>& atqa, uint8_t sak);

  DesfireReader(Transceive transceive, const Config& config);
  ~DesfireReader();

  /** Runs select → authenticate → read. Returns the file content on success. */
  std::optional<std::vector<uint8_t>> readFile();

private:
  bool command(uint8_t ins, const std::vector<uint8_t>& data, std::vector<uint8_t>& out, uint8_t& status);
  bool authenticate();
  size_t blockSize() const { return m_cfg.keyType == KeyType::AES128 ? 16 : 8; }

  bool setKey(const std::vector<uint8_t>& key);
  void ecb(bool encrypt, const uint8_t* in, uint8_t* out);
  void cbcEncrypt(std::vector<uint8_t>& data);   // uses and updates m_iv
  void cbcDecrypt(std::vector<uint8_t>& data);   // uses and updates m_iv
  std::vector<uint8_t> cmac(const std::vector<uint8_t>& msg);  // uses and updates m_iv

  static uint32_t crc32(const uint8_t* data, size_t len);

  Transceive m_transceive;
  Config m_cfg;
  std::vector<uint8_t> m_iv;
  std::vector<uint8_t> m_activeKey;
  static const char* TAG;
};
