#pragma once
// Builds ZIP archives for the tests, including deliberately hostile ones. DEFLATE goes through the
// same Windows Compression API the reader uses, so both sides of method 8 are exercised.

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

#include "core/zip.h"
#include "miniz.h"

namespace testzip {

struct Item {
    std::string name;
    std::string data;
    bool deflate = false;
    bool encrypted = false;
    bool badCrc = false;
    bool zip64Size = false;
    bool directory = false;
    bool symlink = false;
};

inline std::string Deflate(const std::string& data) {
    std::size_t outLength = 0;
    void* heap = tdefl_compress_mem_to_heap(data.data(), data.size(), &outLength, TDEFL_DEFAULT_MAX_PROBES);
    if (!heap) return {};
    std::string out(static_cast<const char*>(heap), outLength);
    mz_free(heap);
    return out;
}

inline void Put16(std::string& out, std::uint16_t value) {
    out.push_back(static_cast<char>(value & 0xFF));
    out.push_back(static_cast<char>((value >> 8) & 0xFF));
}
inline void Put32(std::string& out, std::uint32_t value) {
    out.push_back(static_cast<char>(value & 0xFF));
    out.push_back(static_cast<char>((value >> 8) & 0xFF));
    out.push_back(static_cast<char>((value >> 16) & 0xFF));
    out.push_back(static_cast<char>((value >> 24) & 0xFF));
}

inline bool Write(const std::wstring& path, const std::vector<Item>& items, std::string& error) {
    std::string archive;
    struct Central {
        Item item;
        std::string stored;
        std::uint32_t crc = 0;
        std::uint32_t offset = 0;
    };
    std::vector<Central> central;
    for (const Item& item : items) {
        Central entry;
        entry.item = item;
        entry.stored = item.deflate ? Deflate(item.data) : item.data;
        if (item.deflate && entry.stored.empty()) {
            error = "deflate failed";
            return false;
        }
        entry.crc = blueward::Crc32(item.data.data(), item.data.size());
        if (item.badCrc) entry.crc ^= 0xFFFFFFFFu;
        entry.offset = static_cast<std::uint32_t>(archive.size());

        Put32(archive, 0x04034b50);
        Put16(archive, 20);
        Put16(archive, static_cast<std::uint16_t>(item.encrypted ? 1 : 0));
        Put16(archive, static_cast<std::uint16_t>(item.deflate ? 8 : 0));
        Put16(archive, 0);
        Put16(archive, 0);
        Put32(archive, entry.crc);
        Put32(archive, item.zip64Size ? 0xFFFFFFFFu : static_cast<std::uint32_t>(entry.stored.size()));
        Put32(archive, item.zip64Size ? 0xFFFFFFFFu : static_cast<std::uint32_t>(item.data.size()));
        Put16(archive, static_cast<std::uint16_t>(item.name.size()));
        Put16(archive, 0);
        archive += item.name;
        archive += entry.stored;
        central.push_back(std::move(entry));
    }
    const std::uint32_t centralOffset = static_cast<std::uint32_t>(archive.size());
    for (const Central& entry : central) {
        Put32(archive, 0x02014b50);
        Put16(archive, 20);
        Put16(archive, 20);
        Put16(archive, static_cast<std::uint16_t>(entry.item.encrypted ? 1 : 0));
        Put16(archive, static_cast<std::uint16_t>(entry.item.deflate ? 8 : 0));
        Put16(archive, 0);
        Put16(archive, 0);
        Put32(archive, entry.crc);
        Put32(archive, entry.item.zip64Size ? 0xFFFFFFFFu : static_cast<std::uint32_t>(entry.stored.size()));
        Put32(archive, entry.item.zip64Size ? 0xFFFFFFFFu : static_cast<std::uint32_t>(entry.item.data.size()));
        Put16(archive, static_cast<std::uint16_t>(entry.item.name.size()));
        Put16(archive, 0);
        Put16(archive, 0);
        Put16(archive, 0);
        Put16(archive, 0);
        const std::uint32_t external =
            (entry.item.symlink ? 0xA1FF0000u : 0u) | (entry.item.directory ? FILE_ATTRIBUTE_DIRECTORY : 0u);
        Put32(archive, external);
        Put32(archive, entry.offset);
        archive += entry.item.name;
    }
    const std::uint32_t centralSize = static_cast<std::uint32_t>(archive.size()) - centralOffset;
    Put32(archive, 0x06054b50);
    Put16(archive, 0);
    Put16(archive, 0);
    Put16(archive, static_cast<std::uint16_t>(central.size()));
    Put16(archive, static_cast<std::uint16_t>(central.size()));
    Put32(archive, centralSize);
    Put32(archive, centralOffset);
    Put16(archive, 0);

    std::string writeError;
    blueward::MakeDirs(blueward::ParentDir(path));
    if (!blueward::WriteFileAtomic(path, archive, writeError)) {
        error = writeError;
        return false;
    }
    return true;
}

// Cuts bytes off the end of a valid archive (used to break the central directory).
inline bool Truncate(const std::wstring& path, std::size_t bytes) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    GetFileSizeEx(file, &size);
    if (static_cast<std::uint64_t>(bytes) >= static_cast<std::uint64_t>(size.QuadPart)) {
        CloseHandle(file);
        return false;
    }
    LARGE_INTEGER at{};
    at.QuadPart = size.QuadPart - static_cast<LONGLONG>(bytes);
    SetFilePointerEx(file, at, nullptr, FILE_BEGIN);
    const bool ok = SetEndOfFile(file) != FALSE;
    CloseHandle(file);
    return ok;
}

}  // namespace testzip
