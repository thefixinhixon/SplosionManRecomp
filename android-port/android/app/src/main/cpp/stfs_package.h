// stfs_package.h - minimal STFS (Xbox 360 content package) reader.
//
// Standard-library port of the house launcher's Qt-based StfsPackage
// (byte-verified there against real XBLA packages on 2026-10-03). STFS is
// the container format behind XBLA downloads: files start with a "LIVE",
// "PIRS" or "CON " magic and store their payload in 0x1000-byte blocks,
// interleaved with SHA-1 hash tables. This reader lets the Android app
// import a raw XBLA package itself instead of requiring the user to
// extract it on a PC first.
#pragma once

#include <cstdint>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

class StfsPackage {
 public:
  struct Entry {
    std::string path;  // Relative, "/" separated, no leading slash
    bool is_dir = false;
    uint32_t size = 0;
  };

  // Called after each file: (files extracted, total files, name).
  using ProgressCallback =
      std::function<void(int64_t done, int64_t total, const std::string& name)>;

  StfsPackage() = default;

  // Parse header + file table. The file stays open for extraction.
  bool Open(const std::string& path, std::string* error = nullptr);
  bool IsOpen() const { return file_.is_open(); }

  std::string TitleId() const { return title_id_; }  // e.g. "5841098F"
  std::string Magic() const { return magic_; }       // LIVE / PIRS / CON
  std::vector<Entry> Entries() const;

  // Extract every file under dest_dir (created if needed), streaming block
  // by block - the package is never loaded into memory whole.
  bool ExtractAll(const std::string& dest_dir,
                  const ProgressCallback& progress = {},
                  std::string* error = nullptr);

  // SHA-1 verification of every block read during extraction (plus the
  // volume's root hash chain). Default OFF: it roughly doubles extraction
  // I/O cost. Turn on when diagnosing a suspect package.
  void SetVerifyHashes(bool on) { verify_hashes_ = on; }
  bool VerifyHashes() const { return verify_hashes_; }

 private:
  struct RawEntry {
    std::string name;
    bool is_dir = false;
    uint16_t parent = 0xFFFF;  // File-table index, 0xFFFF = root
    uint32_t start_block = 0;
    uint32_t blocks_allocated = 0;
    uint32_t size = 0;
  };

  bool ReadAt(int64_t offset, int64_t size, std::vector<uint8_t>* out,
              std::string* error);
  bool HashEntry(uint32_t block, std::vector<uint8_t>* hash, uint32_t* next,
                 std::string* error);
  int64_t DataBlockOffset(uint32_t block) const;
  int64_t HashTableOffset(int level, uint32_t table_index) const;
  int TreeLevels() const;
  bool ParseFileTable(std::string* error);
  std::string EntryPath(int index) const;

  std::ifstream file_;
  int64_t file_size_ = 0;
  std::string magic_;
  std::string title_id_;
  int64_t data_start_ = 0;         // First hash table's file offset
  uint32_t data_block_count_ = 0;  // Payload blocks (hash tables excluded)
  uint32_t data_block_offset_ = 0;  // Added to stored block numbers (0 here)
  uint32_t file_table_block_ = 0;
  std::vector<uint8_t> root_hash_;  // SHA-1 of the top-level hash table
  std::vector<RawEntry> raw_entries_;  // Slot order == file-table order
  bool verify_hashes_ = false;
};
