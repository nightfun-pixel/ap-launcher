#include "core/zip.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <set>

#include "core/fileio.h"
#include "core/text.h"
#include "miniz.h"

namespace blueward {

namespace {

constexpr std::uint32_t kLocalSignature = 0x04034b50;
constexpr std::uint32_t kCentralSignature = 0x02014b50;
constexpr std::uint32_t kEndSignature = 0x06054b50;
constexpr std::uint32_t kMaxEntries = 6;
constexpr std::uint64_t kMaxEntrySize = 1ull << 30;
constexpr std::uint64_t kCentralCap = 8ull << 20;

std::uint16_t Read16(const unsigned char* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }
std::uint32_t Read32(const unsigned char* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) | (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

bool AsciiEntryName(std::string_view name) {
    if (name.empty() || name.size() > 255) return false;
    if (name == "." || name == "..") return false;
    for (const char c : name) {
        const unsigned char u = static_cast<unsigned char>(c);
        const bool ok = (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') || (u >= '0' && u <= '9') || c == '.' || c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
}

std::string LowerAscii(std::string value) {
    for (char& c : value)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return value;
}

}  // namespace

bool IsSafeEntryName(std::string_view name) { return AsciiEntryName(name); }

std::uint32_t Crc32(const void* data, std::size_t size, std::uint32_t seed) {
    static std::uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t value = i;
            for (int bit = 0; bit < 8; ++bit) value = (value & 1) ? (0xEDB88320u ^ (value >> 1)) : (value >> 1);
            table[i] = value;
        }
        ready = true;
    }
    const auto* bytes = static_cast<const unsigned char*>(data);
    std::uint32_t crc = seed ^ 0xFFFFFFFFu;
    for (std::size_t i = 0; i < size; ++i) crc = table[(crc ^ bytes[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

ZipReader::~ZipReader() { Close(); }

void ZipReader::Close() {
    if (file_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(file_));
        file_ = nullptr;
    }
    entries_.clear();
    fileSize_ = 0;
}

bool ZipReader::ReadAt(std::uint64_t offset, void* buffer, std::size_t size, std::string& error) const {
    if (offset + size > fileSize_) {
        error = "the archive is truncated";
        return false;
    }
    HANDLE file = static_cast<HANDLE>(file_);
    LARGE_INTEGER at{};
    at.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(file, at, nullptr, FILE_BEGIN)) {
        error = "cannot seek in the archive";
        return false;
    }
    std::size_t done = 0;
    while (done < size) {
        DWORD got = 0;
        if (!ReadFile(file, static_cast<unsigned char*>(buffer) + done, static_cast<DWORD>(size - done), &got, nullptr) || got == 0) {
            error = "cannot read the archive";
            return false;
        }
        done += got;
    }
    return true;
}

bool ZipReader::Open(const std::wstring& path, std::string& error, std::string* code) {
    Close();
    error.clear();
    if (code) code->clear();
    auto fail = [&](const char* errorCode, std::string text) {
        if (code) *code = errorCode;
        error = std::move(text);
        Close();
        return false;
    };
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return fail("E_ZIP_OPEN", "cannot open the archive (Windows " + std::to_string(GetLastError()) + ")");
    file_ = file;
    LARGE_INTEGER size{};
    GetFileSizeEx(file, &size);
    fileSize_ = static_cast<std::uint64_t>(size.QuadPart);
    if (fileSize_ < 22) return fail("E_ZIP_OPEN", "the archive is too small to be a zip");
    const std::uint64_t tailSize = std::min<std::uint64_t>(fileSize_, 22 + 65535);
    std::vector<unsigned char> tail(static_cast<std::size_t>(tailSize));
    if (!ReadAt(fileSize_ - tailSize, tail.data(), tail.size(), error)) return fail("E_ZIP_OPEN", error);
    std::size_t eocd = std::string::npos;
    for (std::size_t i = tail.size() - 22 + 1; i-- > 0;) {
        if (Read32(tail.data() + i) == kEndSignature) {
            const std::uint16_t comment = Read16(tail.data() + i + 20);
            if (i + 22 + comment <= tail.size()) {
                eocd = i;
                break;
            }
        }
    }
    if (eocd == std::string::npos) return fail("E_ZIP_OPEN", "the archive has no end-of-central-directory record");
    const unsigned char* end = tail.data() + eocd;
    const std::uint16_t disk = Read16(end + 4), centralDisk = Read16(end + 6);
    const std::uint16_t onDisk = Read16(end + 8), total = Read16(end + 10);
    const std::uint32_t centralSize = Read32(end + 12), centralOffset = Read32(end + 16);
    if (disk != 0 || centralDisk != 0 || onDisk != total) return fail("E_ZIP_OPEN", "multi-volume archives are not supported");
    if (total < 1 || total > kMaxEntries) return fail("E_ZIP_COUNT", "the archive must have 1 to 6 entries");
    if (centralSize > kCentralCap || static_cast<std::uint64_t>(centralOffset) + centralSize > fileSize_)
        return fail("E_ZIP_OPEN", "the central directory is out of bounds");
    std::vector<unsigned char> central(centralSize);
    if (!ReadAt(centralOffset, central.data(), central.size(), error)) return fail("E_ZIP_OPEN", error);
    std::size_t at = 0;
    std::set<std::string> names;
    for (std::uint16_t index = 0; index < total; ++index) {
        if (at + 46 > central.size() || Read32(central.data() + at) != kCentralSignature)
            return fail("E_ZIP_OPEN", "the central directory is damaged");
        const unsigned char* entry = central.data() + at;
        const std::uint16_t flags = Read16(entry + 8);
        const std::uint16_t method = Read16(entry + 10);
        const std::uint32_t crc = Read32(entry + 16);
        const std::uint32_t compressed = Read32(entry + 20);
        const std::uint32_t uncompressed = Read32(entry + 24);
        const std::uint16_t nameLength = Read16(entry + 28), extraLength = Read16(entry + 30), commentLength = Read16(entry + 32);
        const std::uint32_t external = Read32(entry + 38);
        const std::uint32_t localOffset = Read32(entry + 42);
        if (at + 46 + nameLength + extraLength + commentLength > central.size())
            return fail("E_ZIP_OPEN", "the central directory is truncated");
        if ((flags & 0x0001) != 0) return fail("E_ZIP_OPEN", "encrypted zip entries are not supported");
        if (method != 0 && method != 8) return fail("E_ZIP_METHOD", "unsupported zip compression method");
        if (compressed == 0xFFFFFFFFu || uncompressed == 0xFFFFFFFFu || localOffset == 0xFFFFFFFFu)
            return fail("E_ZIP_OPEN", "ZIP64 archives are not supported");
        if (uncompressed > kMaxEntrySize || compressed > kMaxEntrySize) return fail("E_ZIP_OPEN", "a zip entry is larger than 1 GiB");
        const std::string name(reinterpret_cast<const char*>(entry + 46), nameLength);
        if (name.empty() || name.back() == '/') return fail("E_ZIP_TYPE", "directory entries are not allowed");
        if ((external & FILE_ATTRIBUTE_DIRECTORY) != 0 || ((external >> 16) & 0xF000) == 0xA000)
            return fail("E_ZIP_TYPE", "symlink and directory entries are not allowed");
        if (!AsciiEntryName(name)) return fail("E_ZIP_NAME", "unsafe zip entry name: " + name);
        const std::string lower = LowerAscii(name);
        if (!names.insert(lower).second) return fail("E_ZIP_DUPLICATE", "duplicate zip entry name: " + name);
        ZipEntry zipEntry;
        zipEntry.name = name;
        zipEntry.method = method;
        zipEntry.crc = crc;
        zipEntry.compressedSize = compressed;
        zipEntry.size = uncompressed;
        zipEntry.localOffset = localOffset;
        zipEntry.externalAttributes = external;
        entries_.push_back(std::move(zipEntry));
        at += 46 + nameLength + extraLength + commentLength;
    }
    return true;
}

bool ZipReader::Read(const ZipEntry& entry, std::vector<unsigned char>& out, std::string& error) const {
    out.clear();
    error.clear();
    if (file_ == nullptr) {
        error = "the archive is not open";
        return false;
    }
    unsigned char header[30];
    if (!ReadAt(entry.localOffset, header, sizeof header, error)) return false;
    if (Read32(header) != kLocalSignature) {
        error = "the local file header is damaged";
        return false;
    }
    const std::uint16_t flags = Read16(header + 6);
    const std::uint16_t method = Read16(header + 8);
    const std::uint16_t nameLength = Read16(header + 26), extraLength = Read16(header + 28);
    if ((flags & 0x0001) != 0 || method != entry.method) {
        error = "the local file header disagrees with the central directory";
        return false;
    }
    const std::uint64_t dataOffset = entry.localOffset + 30 + nameLength + extraLength;
    if (dataOffset + entry.compressedSize > fileSize_) {
        error = "the zip entry is out of bounds";
        return false;
    }
    std::vector<unsigned char> compressed(static_cast<std::size_t>(entry.compressedSize));
    if (!compressed.empty() && !ReadAt(dataOffset, compressed.data(), compressed.size(), error)) return false;

    if (entry.method == 0) {
        if (entry.compressedSize != entry.size) {
            error = "a stored zip entry has mismatched sizes";
            return false;
        }
        out = std::move(compressed);
    } else {
        out.resize(static_cast<std::size_t>(entry.size));
        if (entry.size == 0) {
            out.clear();
        } else {
            const std::size_t produced = tinfl_decompress_mem_to_mem(out.data(), out.size(), compressed.data(), compressed.size(), 0);
            if (produced == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED || produced != entry.size) {
                error = "cannot inflate a zip entry";
                out.clear();
                return false;
            }
        }
    }
    if (Crc32(out.data(), out.size()) != entry.crc) {
        error = "CRC-32 mismatch in a zip entry";
        out.clear();
        return false;
    }
    return true;
}

bool ExtractAllowed(const std::wstring& zipPath, const std::vector<std::string>& allowed, const std::wstring& destDir, std::string& error,
                    std::string* code) {
    error.clear();
    if (code) code->clear();
    ZipReader reader;
    if (!reader.Open(zipPath, error, code)) {
        if (code && code->empty()) *code = "E_ZIP_OPEN";
        return false;
    }
    std::set<std::string> wanted(allowed.begin(), allowed.end());
    std::set<std::string> seen;
    for (const ZipEntry& entry : reader.Entries()) {
        if (wanted.find(entry.name) != wanted.end()) seen.insert(entry.name);
    }
    for (const std::string& name : wanted) {
        if (seen.find(name) == seen.end()) {
            if (code) *code = "E_ZIP_MISSING";
            error = "the archive is missing " + name;
            return false;
        }
    }
    if (!MakeDirs(destDir)) {
        if (code) *code = "E_ZIP_EXTRACT";
        error = "cannot create the staging folder";
        return false;
    }
    for (const ZipEntry& entry : reader.Entries()) {
        if (wanted.find(entry.name) == wanted.end()) continue;  // a validated sixth entry is ignored
        std::vector<unsigned char> bytes;
        std::string readError;
        if (!reader.Read(entry, bytes, readError)) {
            if (code) *code = "E_ZIP_CRC";
            error = "cannot read " + entry.name + ": " + readError;
            return false;
        }
        const std::wstring target = destDir + L"\\" + Widen(entry.name);
        std::string writeError;
        if (!WriteFileAtomic(target, std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()), writeError)) {
            if (code) *code = "E_ZIP_EXTRACT";
            error = "cannot write " + entry.name + ": " + writeError;
            return false;
        }
    }
    return true;
}

}  // namespace blueward
