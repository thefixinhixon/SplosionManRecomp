// sha1.cpp - see sha1.h.
#include "sha1.h"

#include <cstring>

namespace {

inline uint32_t Rol(uint32_t value, int bits) {
  return (value << bits) | (value >> (32 - bits));
}

}  // namespace

void Sha1::Reset() {
  state_[0] = 0x67452301u;
  state_[1] = 0xEFCDAB89u;
  state_[2] = 0x98BADCFEu;
  state_[3] = 0x10325476u;
  state_[4] = 0xC3D2E1F0u;
  length_ = 0;
  buffer_size_ = 0;
}

void Sha1::ProcessBlock(const uint8_t* block) {
  uint32_t w[80];
  for (int i = 0; i < 16; ++i) {
    w[i] = (uint32_t(block[i * 4]) << 24) | (uint32_t(block[i * 4 + 1]) << 16) |
           (uint32_t(block[i * 4 + 2]) << 8) | uint32_t(block[i * 4 + 3]);
  }
  for (int i = 16; i < 80; ++i) {
    w[i] = Rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
  }

  uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3], e = state_[4];
  for (int i = 0; i < 80; ++i) {
    uint32_t f, k;
    if (i < 20) {
      f = (b & c) | ((~b) & d);
      k = 0x5A827999u;
    } else if (i < 40) {
      f = b ^ c ^ d;
      k = 0x6ED9EBA1u;
    } else if (i < 60) {
      f = (b & c) | (b & d) | (c & d);
      k = 0x8F1BBCDCu;
    } else {
      f = b ^ c ^ d;
      k = 0xCA62C1D6u;
    }
    uint32_t temp = Rol(a, 5) + f + e + k + w[i];
    e = d;
    d = c;
    c = Rol(b, 30);
    b = a;
    a = temp;
  }
  state_[0] += a;
  state_[1] += b;
  state_[2] += c;
  state_[3] += d;
  state_[4] += e;
}

void Sha1::Update(const uint8_t* data, size_t length) {
  length_ += length;
  while (length > 0) {
    size_t take = 64 - buffer_size_;
    if (take > length) take = length;
    std::memcpy(buffer_ + buffer_size_, data, take);
    buffer_size_ += take;
    data += take;
    length -= take;
    if (buffer_size_ == 64) {
      ProcessBlock(buffer_);
      buffer_size_ = 0;
    }
  }
}

std::array<uint8_t, 20> Sha1::Final() {
  uint64_t bit_length = length_ * 8;
  uint8_t pad_byte = 0x80;
  Update(&pad_byte, 1);
  uint8_t zero = 0;
  while (buffer_size_ != 56) {
    Update(&zero, 1);
  }
  // Append the length manually: Update() would disturb length_, so write the
  // final block directly instead of going through the buffered path again.
  // At this point buffer_ holds exactly 56 bytes; complete it by hand.
  for (int i = 0; i < 8; ++i) {
    buffer_[56 + i] = uint8_t(bit_length >> (56 - i * 8));
  }
  ProcessBlock(buffer_);

  std::array<uint8_t, 20> digest;
  for (int i = 0; i < 5; ++i) {
    digest[i * 4] = uint8_t(state_[i] >> 24);
    digest[i * 4 + 1] = uint8_t(state_[i] >> 16);
    digest[i * 4 + 2] = uint8_t(state_[i] >> 8);
    digest[i * 4 + 3] = uint8_t(state_[i]);
  }
  Reset();
  return digest;
}

std::array<uint8_t, 20> Sha1Hash(const uint8_t* data, size_t length) {
  Sha1 sha;
  sha.Update(data, length);
  return sha.Final();
}
