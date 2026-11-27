#pragma once
// SHA-256 through CNG BCrypt. A file is hashed in 1 MiB chunks with sequential-scan hints.

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace blueward {

bool Sha256Bytes(const void* data, std::size_t size, std::string& hex);
bool Sha256String(std::string_view text, std::string& hex);

// `progress(done, total)` returning false aborts the hash. `error` carries a Win32 code.
bool Sha256File(const std::wstring& path, std::string& hex, const std::function<bool(std::uint64_t, std::uint64_t)>& progress,
                std::string& error);
bool Sha256File(const std::wstring& path, std::string& hex);

bool IsSha256Hex(std::string_view value);  // exactly 64 lower-case hex characters
std::string LowerHex(std::string_view value);

}  // namespace blueward
