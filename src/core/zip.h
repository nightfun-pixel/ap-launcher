#pragma once
// ZIP reader for the patch archive. Method 0 (store) and method 8 (raw DEFLATE through the Windows
// Compression API) only. ZIP64, encryption, absolute paths, backslashes and `..` are refused before
// a single byte is written. Extraction accepts exactly an explicit allowlist of root-level names.

#include <cstdint>
#include <string>
#include <vector>

namespace blueward {

struct ZipEntry {
    std::string name;
    std::uint16_t method = 0;
    std::uint32_t crc = 0;
    std::uint64_t compressedSize = 0;
    std::uint64_t size = 0;
    std::uint64_t localOffset = 0;
    std::uint32_t externalAttributes = 0;
};

class ZipReader {
public:
    ZipReader() = default;
    ZipReader(const ZipReader&) = delete;
    ZipReader& operator=(const ZipReader&) = delete;
    ~ZipReader();

    bool Open(const std::wstring& path, std::string& error, std::string* code = nullptr);
    const std::vector<ZipEntry>& Entries() const { return entries_; }
    // Decompresses one entry, checks CRC-32 and returns the bytes. Rejects unsupported methods.
    bool Read(const ZipEntry& entry, std::vector<unsigned char>& out, std::string& error) const;

private:
    void Close();
    bool ReadAt(std::uint64_t offset, void* buffer, std::size_t size, std::string& error) const;

    void* file_ = nullptr;  // HANDLE
    std::uint64_t fileSize_ = 0;
    std::vector<ZipEntry> entries_;
};

// Opens `zipPath`, requires every entry to be one of `allowed` (exactly, at the root, no folders),
// requires every allowed name to be present, and writes the files into `destDir`. A sixth entry is
// validated and ignored (SPEC FR-41).
bool ExtractAllowed(const std::wstring& zipPath, const std::vector<std::string>& allowed, const std::wstring& destDir,
                    std::string& error, std::string* code = nullptr);

// A missing or malformed name is refused by Open; this is the shared rule used by both sides.
bool IsSafeEntryName(std::string_view name);

std::uint32_t Crc32(const void* data, std::size_t size, std::uint32_t seed = 0);

}  // namespace blueward
