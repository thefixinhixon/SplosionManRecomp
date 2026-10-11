// sha1.h - minimal SHA-1 implementation (FIPS 180-1) for the STFS reader.
// Only used to verify STFS hash tables when hash verification is enabled.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

class Sha1 {
 public:
  Sha1() { Reset(); }
  void Reset();
  void Update(const uint8_t* data, size_t length);
  // Writes the 20-byte digest and resets the context.
  std::array<uint8_t, 20> Final();

 private:
  void ProcessBlock(const uint8_t* block);

  uint32_t state_[5];
  uint64_t length_ = 0;
  uint8_t buffer_[64];
  size_t buffer_size_ = 0;
};

// One-shot convenience.
std::array<uint8_t, 20> Sha1Hash(const uint8_t* data, size_t length);
