#include "DesfireReader.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <esp_log.h>
#include <esp_random.h>
#include "mbedtls/build_info.h"
#include "mbedtls/aes.h"
#include "mbedtls/platform_util.h"
#if defined(MBEDTLS_DES_C)
#include "mbedtls/des.h"
#endif

const char* DesfireReader::TAG = "DesfireReader";

namespace {
constexpr uint8_t kStatusOk = 0x00;
constexpr uint8_t kStatusMoreFrames = 0xAF;
constexpr size_t kMaxResponse = 1024;

std::optional<std::vector<uint8_t>> parseHex(const std::string& hex) {
  if (hex.size() % 2 != 0) return std::nullopt;
  std::vector<uint8_t> out;
  out.reserve(hex.size() / 2);
  for (size_t i = 0; i < hex.size(); i += 2) {
    if (!std::isxdigit(static_cast<unsigned char>(hex[i])) ||
        !std::isxdigit(static_cast<unsigned char>(hex[i + 1]))) {
      return std::nullopt;
    }
    out.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
  }
  return out;
}

void rotateLeft(std::vector<uint8_t>& v) {
  std::rotate(v.begin(), v.begin() + 1, v.end());
}

void shiftLeftOne(std::vector<uint8_t>& v) {
  uint8_t carry = 0;
  for (size_t i = v.size(); i-- > 0;) {
    const uint8_t next = v[i] >> 7;
    v[i] = static_cast<uint8_t>((v[i] << 1) | carry);
    carry = next;
  }
}
}  // namespace

std::optional<DesfireReader::Config> DesfireReader::makeConfig(const std::string& aidHex, uint8_t fileNo,
                                                               uint8_t keyNo, uint8_t keyType,
                                                               uint8_t commMode, uint16_t readLength,
                                                               const std::string& keyHex) {
  Config cfg;
  auto aid = parseHex(aidHex);
  auto key = parseHex(keyHex);
  if (!aid || aid->size() != 3 || !key) return std::nullopt;
  if (keyType > 2 || (commMode != 0 && commMode != 1 && commMode != 3)) return std::nullopt;
  cfg.keyType = static_cast<KeyType>(keyType);
  const size_t expectedKeyLen = cfg.keyType == KeyType::TDES3K ? 24 : 16;
  if (key->size() != expectedKeyLen) return std::nullopt;
  // AID is shown MSB-first in the UI, but DESFire expects it LSB-first.
  cfg.aid = {(*aid)[2], (*aid)[1], (*aid)[0]};
  cfg.fileNo = fileNo;
  cfg.keyNo = keyNo;
  cfg.commMode = static_cast<CommMode>(commMode);
  cfg.readLength = std::min<uint16_t>(readLength, 256);
  cfg.key = std::move(*key);
  return cfg;
}

bool DesfireReader::looksLikeDesfire(const std::array<uint8_t, 2>& atqa, uint8_t sak) {
  (void)atqa;
  return (sak & 0x20) != 0;
}

DesfireReader::DesfireReader(Transceive transceive, const Config& config)
    : m_transceive(std::move(transceive)), m_cfg(config) {}

DesfireReader::~DesfireReader() {
  if (!m_activeKey.empty()) mbedtls_platform_zeroize(m_activeKey.data(), m_activeKey.size());
  if (!m_cfg.key.empty()) mbedtls_platform_zeroize(m_cfg.key.data(), m_cfg.key.size());
}

bool DesfireReader::command(uint8_t ins, const std::vector<uint8_t>& data, std::vector<uint8_t>& out,
                            uint8_t& status) {
  out.clear();
  uint8_t nextIns = ins;
  const std::vector<uint8_t>* payload = &data;
  const std::vector<uint8_t> empty;
  while (true) {
    std::vector<uint8_t> apdu = {0x90, nextIns, 0x00, 0x00};
    if (!payload->empty()) {
      apdu.push_back(static_cast<uint8_t>(payload->size()));
      apdu.insert(apdu.end(), payload->begin(), payload->end());
    }
    apdu.push_back(0x00);
    std::vector<uint8_t> resp;
    if (!m_transceive(apdu, resp) || resp.size() < 2 || resp[resp.size() - 2] != 0x91) {
      ESP_LOGW(TAG, "Command 0x%02X: transceive failed", ins);
      return false;
    }
    status = resp.back();
    out.insert(out.end(), resp.begin(), resp.end() - 2);
    if (out.size() > kMaxResponse) {
      ESP_LOGW(TAG, "Command 0x%02X: response too large", ins);
      return false;
    }
    // Authentication uses 0xAF as a protocol step, so only auto-chain other commands.
    if (status != kStatusMoreFrames || ins == 0xAA || ins == 0x1A) return true;
    nextIns = 0xAF;
    payload = &empty;
  }
}

bool DesfireReader::setKey(const std::vector<uint8_t>& key) {
  m_activeKey = key;
  m_iv.assign(blockSize(), 0);
#if !defined(MBEDTLS_DES_C)
  if (m_cfg.keyType != KeyType::AES128) {
    ESP_LOGE(TAG, "3DES keys need CONFIG_MBEDTLS_DES_C=y (menuconfig → mbedTLS → DES)");
    return false;
  }
#endif
  return true;
}

void DesfireReader::ecb(bool encrypt, const uint8_t* in, uint8_t* out) {
  if (m_cfg.keyType == KeyType::AES128) {
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    if (encrypt) {
      mbedtls_aes_setkey_enc(&ctx, m_activeKey.data(), 128);
    } else {
      mbedtls_aes_setkey_dec(&ctx, m_activeKey.data(), 128);
    }
    mbedtls_aes_crypt_ecb(&ctx, encrypt ? MBEDTLS_AES_ENCRYPT : MBEDTLS_AES_DECRYPT, in, out);
    mbedtls_aes_free(&ctx);
    return;
  }
#if defined(MBEDTLS_DES_C)
  mbedtls_des3_context ctx;
  mbedtls_des3_init(&ctx);
  if (m_activeKey.size() == 24) {
    encrypt ? mbedtls_des3_set3key_enc(&ctx, m_activeKey.data()) : mbedtls_des3_set3key_dec(&ctx, m_activeKey.data());
  } else {
    encrypt ? mbedtls_des3_set2key_enc(&ctx, m_activeKey.data()) : mbedtls_des3_set2key_dec(&ctx, m_activeKey.data());
  }
  mbedtls_des3_crypt_ecb(&ctx, in, out);
  mbedtls_des3_free(&ctx);
#else
  std::memset(out, 0, blockSize());
#endif
}

void DesfireReader::cbcEncrypt(std::vector<uint8_t>& data) {
  const size_t bs = blockSize();
  for (size_t off = 0; off < data.size(); off += bs) {
    for (size_t i = 0; i < bs; ++i) data[off + i] ^= m_iv[i];
    ecb(true, &data[off], &data[off]);
    std::copy(data.begin() + off, data.begin() + off + bs, m_iv.begin());
  }
}

void DesfireReader::cbcDecrypt(std::vector<uint8_t>& data) {
  const size_t bs = blockSize();
  std::vector<uint8_t> cipher(bs);
  for (size_t off = 0; off < data.size(); off += bs) {
    std::copy(data.begin() + off, data.begin() + off + bs, cipher.begin());
    ecb(false, &data[off], &data[off]);
    for (size_t i = 0; i < bs; ++i) data[off + i] ^= m_iv[i];
    m_iv = cipher;
  }
}

std::vector<uint8_t> DesfireReader::cmac(const std::vector<uint8_t>& msg) {
  const size_t bs = blockSize();
  const uint8_t rb = bs == 16 ? 0x87 : 0x1B;
  std::vector<uint8_t> k1(bs, 0);
  ecb(true, k1.data(), k1.data());
  const bool msb1 = (k1[0] & 0x80) != 0;
  shiftLeftOne(k1);
  if (msb1) k1[bs - 1] ^= rb;
  std::vector<uint8_t> k2 = k1;
  const bool msb2 = (k2[0] & 0x80) != 0;
  shiftLeftOne(k2);
  if (msb2) k2[bs - 1] ^= rb;

  std::vector<uint8_t> m = msg;
  const bool complete = !m.empty() && m.size() % bs == 0;
  if (!complete) {
    m.push_back(0x80);
    while (m.size() % bs != 0) m.push_back(0x00);
  }
  const std::vector<uint8_t>& sub = complete ? k1 : k2;
  for (size_t i = 0; i < bs; ++i) m[m.size() - bs + i] ^= sub[i];
  cbcEncrypt(m);
  return m_iv;
}

uint32_t DesfireReader::crc32(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return crc;  // DESFire EV1: no final XOR
}

bool DesfireReader::authenticate() {
  const bool aes = m_cfg.keyType == KeyType::AES128;
  if (!setKey(m_cfg.key)) return false;
  const size_t rndLen = m_cfg.keyType == KeyType::TDES2K ? 8 : 16;

  std::vector<uint8_t> resp;
  uint8_t status = 0;
  const uint8_t authIns = aes ? 0xAA : 0x1A;
  if (!command(authIns, {m_cfg.keyNo}, resp, status) || status != kStatusMoreFrames || resp.size() != rndLen) {
    ESP_LOGW(TAG, "Authenticate step 1 failed (status 0x%02X, %zu bytes)", status, resp.size());
    return false;
  }
  std::vector<uint8_t> rndB = resp;
  cbcDecrypt(rndB);

  std::vector<uint8_t> rndA(rndLen);
  esp_fill_random(rndA.data(), rndA.size());
  std::vector<uint8_t> rndBRot = rndB;
  rotateLeft(rndBRot);
  std::vector<uint8_t> token = rndA;
  token.insert(token.end(), rndBRot.begin(), rndBRot.end());
  cbcEncrypt(token);

  if (!command(0xAF, token, resp, status) || status != kStatusOk || resp.size() != rndLen) {
    ESP_LOGW(TAG, "Authenticate step 2 failed (status 0x%02X) - wrong key or key number?", status);
    return false;
  }
  cbcDecrypt(resp);
  std::vector<uint8_t> rndARot = rndA;
  rotateLeft(rndARot);
  if (resp != rndARot) {
    ESP_LOGW(TAG, "Authenticate: card response mismatch");
    return false;
  }

  std::vector<uint8_t> session;
  auto add = [&](const std::vector<uint8_t>& src, size_t from, size_t n) {
    session.insert(session.end(), src.begin() + from, src.begin() + from + n);
  };
  if (aes) {
    add(rndA, 0, 4); add(rndB, 0, 4); add(rndA, 12, 4); add(rndB, 12, 4);
  } else if (m_cfg.keyType == KeyType::TDES3K) {
    add(rndA, 0, 4); add(rndB, 0, 4); add(rndA, 6, 4); add(rndB, 6, 4); add(rndA, 12, 4); add(rndB, 12, 4);
  } else {
    bool singleDes = true;
    for (size_t i = 0; i < 8; ++i) singleDes &= (m_cfg.key[i] & 0xFE) == (m_cfg.key[i + 8] & 0xFE);
    add(rndA, 0, 4); add(rndB, 0, 4);
    if (singleDes) { add(rndA, 0, 4); add(rndB, 0, 4); } else { add(rndA, 4, 4); add(rndB, 4, 4); }
  }
  mbedtls_platform_zeroize(rndA.data(), rndA.size());
  mbedtls_platform_zeroize(rndB.data(), rndB.size());
  const bool ok = setKey(session);
  mbedtls_platform_zeroize(session.data(), session.size());
  return ok;
}

std::optional<std::vector<uint8_t>> DesfireReader::readFile() {
  std::vector<uint8_t> resp;
  uint8_t status = 0;
  const std::vector<uint8_t> aid(m_cfg.aid.begin(), m_cfg.aid.end());
  if (!command(0x5A, aid, resp, status) || status != kStatusOk) {
    ESP_LOGW(TAG, "SelectApplication failed (status 0x%02X)", status);
    return std::nullopt;
  }
  if (!authenticate()) return std::nullopt;

  const uint32_t len = m_cfg.readLength;
  const std::vector<uint8_t> params = {m_cfg.fileNo, 0x00, 0x00, 0x00,
                                       static_cast<uint8_t>(len), static_cast<uint8_t>(len >> 8), 0x00};
  std::vector<uint8_t> cmd = {0xBD};
  cmd.insert(cmd.end(), params.begin(), params.end());
  cmac(cmd);  // keeps the session IV in sync with the card

  if (!command(0xBD, params, resp, status) || status != kStatusOk) {
    ESP_LOGW(TAG, "ReadData failed (status 0x%02X)", status);
    return std::nullopt;
  }

  if (m_cfg.commMode == CommMode::Plain) {
    return resp;
  }

  if (m_cfg.commMode == CommMode::Mac) {
    if (resp.size() < 8) return std::nullopt;
    std::vector<uint8_t> data(resp.begin(), resp.end() - 8);
    std::vector<uint8_t> msg = data;
    msg.push_back(kStatusOk);
    const std::vector<uint8_t> mac = cmac(msg);
    if (!std::equal(mac.begin(), mac.begin() + 8, resp.end() - 8)) {
      ESP_LOGW(TAG, "ReadData: MAC verification failed");
      return std::nullopt;
    }
    return data;
  }

  // Fully enciphered: data || CRC32(data || status) || zero padding
  const size_t bs = blockSize();
  if (resp.empty() || resp.size() % bs != 0) {
    ESP_LOGW(TAG, "ReadData: invalid encrypted length %zu", resp.size());
    return std::nullopt;
  }
  cbcDecrypt(resp);
  auto crcMatches = [&](size_t dataLen) {
    if (dataLen + 4 > resp.size()) return false;
    std::vector<uint8_t> msg(resp.begin(), resp.begin() + dataLen);
    msg.push_back(kStatusOk);
    const uint32_t crc = crc32(msg.data(), msg.size());
    for (int i = 0; i < 4; ++i) {
      if (resp[dataLen + i] != static_cast<uint8_t>(crc >> (8 * i))) return false;
    }
    return std::all_of(resp.begin() + dataLen + 4, resp.end(), [](uint8_t b) { return b == 0; });
  };
  std::optional<size_t> dataLen;
  if (len != 0) {
    if (crcMatches(len)) dataLen = len;
  } else {
    const size_t maxLen = resp.size() - 4;
    const size_t minLen = maxLen >= bs - 1 ? maxLen - (bs - 1) : 0;
    for (size_t l = maxLen + 1; l-- > minLen;) {
      if (crcMatches(l)) { dataLen = l; break; }
    }
  }
  if (!dataLen) {
    ESP_LOGW(TAG, "ReadData: CRC verification failed");
    return std::nullopt;
  }
  resp.resize(*dataLen);
  return resp;
}
