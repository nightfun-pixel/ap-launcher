#pragma once
// Test helpers: scratch paths, byte files and a fake Azur Promilia tree.

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

#include "core/fileio.h"
#include "core/sha256.h"
#include "core/text.h"

namespace testutil {

inline std::wstring Path(const std::wstring& leaf) { return g_tempRoot + L"\\" + leaf; }

inline bool Write(const std::wstring& path, const std::string& bytes) {
    std::string error;
    blueward::MakeDirs(blueward::ParentDir(path));
    return blueward::WriteFileAtomic(path, bytes, error);
}

inline bool Write(const std::wstring& path, const std::wstring& text) { return Write(path, blueward::Narrow(text)); }

inline std::string Read(const std::wstring& path) {
    std::string out;
    blueward::ReadFileBytes(path, out);
    return out;
}

inline bool Exists(const std::wstring& path) { return blueward::FileExists(path); }

inline std::string Hash(const std::string& bytes) {
    std::string hex;
    blueward::Sha256String(bytes, hex);
    return hex;
}

inline std::string HashFile(const std::wstring& path) {
    std::string hex;
    blueward::Sha256File(path, hex);
    return hex;
}

inline std::uint64_t Size(const std::wstring& path) { return blueward::FileSize(path); }

inline std::wstring ExeDir() {
    wchar_t buffer[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    return blueward::ParentDir(buffer);
}

}  // namespace testutil
