// stfs_package.cpp - see stfs_package.h for the format notes.
//
// Layout summary (all confirmed against 584108C2033F0001, PIRS, by the
// original Qt implementation this was ported from):
//
//   Header (common to LIVE/PIRS/CON, after the signature region):
//     0x340  u32 BE  headerSize     (0xAD0E in the reference file)
//     0x34C  u64 BE  contentSize    (payload region length)
//     0x360  u32 BE  titleId
//     0x379  volume descriptor (0x24 bytes):
//              +0x00 u8         descriptor size (0x24)
//              +0x04 u24 BE     file table block number (0)
//              +0x08 u8[20]     root hash: SHA-1 of the top hash table
//              +0x1C u32 BE     data block count (1964)
//              +0x20 u32 BE     data block offset (0)
//   Payload starts at headerSize rounded up to a 0x1000 boundary.
//
//   Blocks: payload is a sequence of data blocks of 0x1000 bytes, grouped
//   in 170s, each group PRECEDED by its level-0 hash table block, with
//   higher-level tables wedged in at span boundaries (see DataBlockOffset).
//   A hash table block holds 170 interleaved 24-byte entries:
//              entry = SHA-1 of the data block (20 bytes)
//                    + u32 BE info (0x80 = allocated, low 24 bits = next
//                      block number, 0xFFFFFF = end of chain)
//   File data is a chain: follow next pointers, do not assume files are
//   contiguous.
//
//   File table: a chain of data blocks starting at the descriptor's file
//   table block number, 64 entries of 0x40 bytes per block:
//              +0x00 char name[0x28]
//              +0x28 u8   name length (low 6 bits) + flags (0x80 = dir)
//              +0x29 u24 LE blocks allocated   (LE, not BE!)
//              +0x2F u24 LE start block        (LE, not BE!)
//              +0x32 u16 BE parent entry index (0xFFFF = root)
//              +0x34 u32 BE file size
#include "stfs_package.h"

#include <algorithm>
#include <array>
#include <filesystem>

#include "sha1.h"

namespace {

constexpr int64_t kBlockSize = 0x1000;
constexpr int kBlocksPerGroup = 0xAA;  // 170 data blocks per hash table
constexpr int kHashEntrySize = 24;     // 20-byte SHA-1 + 4-byte info
constexpr uint32_t kChainEnd = 0xFFFFFF;

uint32_t ReadU32BE(const std::vector<uint8_t>& b, size_t off) {
  return (uint32_t(b[off]) << 24) | (uint32_t(b[off + 1]) << 16) |
         (uint32_t(b[off + 2]) << 8) | uint32_t(b[off + 3]);
}
uint32_t ReadU24BE(const std::vector<uint8_t>& b, size_t off) {
  return (uint32_t(b[off]) << 16) | (uint32_t(b[off + 1]) << 8) |
         uint32_t(b[off + 2]);
}
uint32_t ReadU24LE(const std::vector<uint8_t>& b, size_t off) {
  return uint32_t(b[off]) | (uint32_t(b[off + 1]) << 8) |
         (uint32_t(b[off + 2]) << 16);
}
uint16_t ReadU16BE(const std::vector<uint8_t>& b, size_t off) {
  return uint16_t((uint16_t(b[off]) << 8) | uint16_t(b[off + 1]));
}

bool Fail(std::string* error, const std::string& message) {
  if (error) *error = message;
  return false;
}

std::string HexUpper(uint32_t value) {
  const char* digits = "0123456789ABCDEF";
  std::string out(8, '0');
  for (int i = 7; i >= 0; --i) {
    out[i] = digits[value & 0xF];
    value >>= 4;
  }
  return out;
}

}  // namespace

bool StfsPackage::ReadAt(int64_t offset, int64_t size,
                         std::vector<uint8_t>* out, std::string* error) {
  out->assign(static_cast<size_t>(size), 0);
  file_.clear();
  file_.seekg(offset, std::ios::beg);
  if (!file_) {
    return Fail(error, "Seek to " + std::to_string(offset) + " failed");
  }
  file_.read(reinterpret_cast<char*>(out->data()),
             static_cast<std::streamsize>(size));
  if (file_.gcount() != size) {
    return Fail(error, "Short read at offset " + std::to_string(offset));
  }
  return true;
}

int StfsPackage::TreeLevels() const {
  if (data_block_count_ > uint32_t(kBlocksPerGroup * kBlocksPerGroup)) return 3;
  if (data_block_count_ > uint32_t(kBlocksPerGroup)) return 2;
  return 1;
}

int64_t StfsPackage::DataBlockOffset(uint32_t block) const {
  // Count the hash table blocks physically preceding data block N:
  //   - one level-0 table per group, its own group's included;
  //   - level-1 tables: table 0 sits before group 1, table j (j >= 1)
  //     before group 170*j;
  //   - the level-2 root table of a three-level volume sits before
  //     level-1 table 1, i.e. in front of every block from 28900 on.
  const uint64_t group = block / kBlocksPerGroup;
  const uint64_t span = block / (kBlocksPerGroup * kBlocksPerGroup);
  uint64_t physical = uint64_t(block) + group + 1;
  if (group >= 1) physical += span + 1;
  if (TreeLevels() == 3 &&
      block >= uint32_t(kBlocksPerGroup * kBlocksPerGroup))
    physical += 1;
  return data_start_ + int64_t(physical) * kBlockSize;
}

int64_t StfsPackage::HashTableOffset(int level, uint32_t table_index) const {
  if (level == 0) {
    return DataBlockOffset(table_index * kBlocksPerGroup) - kBlockSize;
  }
  if (level == 1) {
    const uint32_t first_l0 =
        table_index == 0 ? 1 : table_index * uint32_t(kBlocksPerGroup);
    return HashTableOffset(0, first_l0) - kBlockSize;
  }
  return HashTableOffset(1, 1) - kBlockSize;
}

bool StfsPackage::HashEntry(uint32_t block, std::vector<uint8_t>* hash,
                            uint32_t* next, std::string* error) {
  std::vector<uint8_t> raw;
  const int64_t off = HashTableOffset(0, block / kBlocksPerGroup) +
                      int64_t(block % kBlocksPerGroup) * kHashEntrySize;
  if (!ReadAt(off, kHashEntrySize, &raw, error)) return false;
  if (hash) hash->assign(raw.begin(), raw.begin() + 20);
  if (next) *next = ReadU32BE(raw, 20) & kChainEnd;
  return true;
}

bool StfsPackage::Open(const std::string& path, std::string* error) {
  file_.open(path, std::ios::binary);
  if (!file_.is_open()) {
    return Fail(error, "Cannot open " + path);
  }
  file_.seekg(0, std::ios::end);
  file_size_ = static_cast<int64_t>(file_.tellg());
  file_.seekg(0, std::ios::beg);

  std::vector<uint8_t> header;
  if (!ReadAt(0, 0x400, &header, error)) return false;

  magic_ = std::string(reinterpret_cast<const char*>(header.data()), 4);
  // Trim trailing spaces ("CON ").
  while (!magic_.empty() && magic_.back() == ' ') magic_.pop_back();
  if (magic_ != "LIVE" && magic_ != "PIRS" && magic_ != "CON") {
    return Fail(error, "Not an STFS package (magic \"" +
                           std::string(reinterpret_cast<const char*>(header.data()), 4) +
                           "\")");
  }

  const uint32_t header_size = ReadU32BE(header, 0x340);
  data_start_ = (int64_t(header_size) + kBlockSize - 1) & ~(kBlockSize - 1);
  if (data_start_ <= 0 || data_start_ >= file_size_) {
    return Fail(error, "Bogus header size " + std::to_string(header_size));
  }

  title_id_ = HexUpper(ReadU32BE(header, 0x360));

  // Volume descriptor at 0x379.
  if (header[0x379] != 0x24) {
    return Fail(error, "Volume descriptor size is " +
                           std::to_string(header[0x379]) + ", expected 0x24");
  }
  file_table_block_ = ReadU24BE(header, 0x379 + 0x04);
  root_hash_.assign(header.begin() + 0x379 + 0x08,
                    header.begin() + 0x379 + 0x08 + 20);
  data_block_count_ = ReadU32BE(header, 0x379 + 0x1C);
  data_block_offset_ = ReadU32BE(header, 0x379 + 0x20);
  if (data_block_count_ == 0) {
    return Fail(error, "Volume declares zero data blocks");
  }

  if (verify_hashes_) {
    std::vector<uint8_t> top;
    if (!ReadAt(HashTableOffset(TreeLevels() - 1, 0), kBlockSize, &top, error))
      return false;
    auto digest = Sha1Hash(top.data(), top.size());
    if (!std::equal(digest.begin(), digest.end(), root_hash_.begin())) {
      return Fail(error, "Root hash mismatch - package is corrupt");
    }
  }

  return ParseFileTable(error);
}

bool StfsPackage::ParseFileTable(std::string* error) {
  raw_entries_.clear();
  uint32_t block = file_table_block_ + data_block_offset_;
  for (uint32_t steps = 0; steps <= data_block_count_; ++steps) {
    if (block >= data_block_count_) {
      return Fail(error, "File table chain leaves the volume");
    }
    std::vector<uint8_t> raw;
    if (!ReadAt(DataBlockOffset(block), kBlockSize, &raw, error)) return false;
    for (int i = 0; i < 64; ++i) {
      const size_t off = size_t(i) * 0x40;
      RawEntry e;
      const uint8_t flags = raw[off + 0x28];
      const int name_length = flags & 0x3F;
      if (name_length == 0) {
        raw_entries_.push_back(e);  // Empty slot; keeps indices honest
        continue;
      }
      e.name.assign(reinterpret_cast<const char*>(raw.data() + off),
                    size_t(name_length));
      e.is_dir = (flags & 0x80) != 0;
      e.blocks_allocated = ReadU24LE(raw, off + 0x29);
      e.start_block = ReadU24LE(raw, off + 0x2F) + data_block_offset_;
      e.parent = ReadU16BE(raw, off + 0x32);
      e.size = ReadU32BE(raw, off + 0x34);
      raw_entries_.push_back(e);
    }
    uint32_t next = 0;
    if (!HashEntry(block, nullptr, &next, error)) return false;
    if (next == kChainEnd) return true;
    block = next;
  }
  return Fail(error, "File table chain never terminates");
}

std::string StfsPackage::EntryPath(int index) const {
  std::vector<std::string> parts;
  int cur = index;
  for (int guard = 0;
       guard < int(raw_entries_.size()) && cur >= 0 &&
       cur < int(raw_entries_.size());
       ++guard) {
    const RawEntry& e = raw_entries_[size_t(cur)];
    if (!e.name.empty()) parts.insert(parts.begin(), e.name);
    if (e.parent == 0xFFFF) break;
    cur = e.parent;
  }
  std::string out;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i) out += "/";
    out += parts[i];
  }
  return out;
}

std::vector<StfsPackage::Entry> StfsPackage::Entries() const {
  std::vector<Entry> out;
  for (size_t i = 0; i < raw_entries_.size(); ++i) {
    const RawEntry& e = raw_entries_[i];
    if (e.name.empty()) continue;
    out.push_back({EntryPath(int(i)), e.is_dir, e.size});
  }
  return out;
}

bool StfsPackage::ExtractAll(const std::string& dest_dir,
                             const ProgressCallback& progress,
                             std::string* error) {
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::create_directories(dest_dir, ec);
  if (ec) {
    return Fail(error, "Cannot create " + dest_dir);
  }

  const std::vector<Entry> list = Entries();
  int64_t done = 0;
  for (size_t i = 0; i < raw_entries_.size(); ++i) {
    const RawEntry& e = raw_entries_[i];
    if (e.name.empty()) continue;
    const std::string path = EntryPath(int(i));
    if (progress) progress(done, int64_t(list.size()), path);
    const fs::path target = fs::path(dest_dir) / path;
    if (e.is_dir) {
      fs::create_directories(target, ec);
      if (ec) return Fail(error, "Cannot create folder " + path);
      ++done;
      continue;
    }

    fs::create_directories(target.parent_path(), ec);
    if (ec) return Fail(error, "Cannot create folder for " + path);
    std::ofstream out(target, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
      return Fail(error, "Cannot write " + target.string());
    }

    // Follow the block chain, streaming one block at a time.
    uint64_t remaining = e.size;
    uint32_t block = e.start_block;
    const uint32_t cap = e.blocks_allocated > 0
                             ? e.blocks_allocated + 1
                             : uint32_t((e.size + kBlockSize - 1) / kBlockSize) + 1;
    for (uint32_t steps = 0; remaining > 0; ++steps) {
      if (block == kChainEnd || block >= data_block_count_ || steps > cap) {
        return Fail(error, "Block chain for " + path + " ends early");
      }
      std::vector<uint8_t> raw;
      if (!ReadAt(DataBlockOffset(block), kBlockSize, &raw, error)) return false;
      if (verify_hashes_) {
        std::vector<uint8_t> want;
        if (!HashEntry(block, &want, nullptr, error)) return false;
        auto digest = Sha1Hash(raw.data(), raw.size());
        if (!std::equal(digest.begin(), digest.end(), want.begin())) {
          return Fail(error, "Hash mismatch in " + path + " (block " +
                                 std::to_string(block) + ")");
        }
      }
      const int64_t chunk = std::min<int64_t>(int64_t(remaining), kBlockSize);
      out.write(reinterpret_cast<const char*>(raw.data()),
                static_cast<std::streamsize>(chunk));
      if (!out) return Fail(error, "Write to " + target.string() + " failed");
      remaining -= uint64_t(chunk);
      uint32_t next = 0;
      if (!HashEntry(block, nullptr, &next, error)) return false;
      block = next;
    }
    out.close();
    ++done;
  }
  if (progress) progress(done, int64_t(list.size()), "");
  return true;
}
