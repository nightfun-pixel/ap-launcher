#include "core/sha256.h"

#include <windows.h>

#include <bcrypt.h>

#include <vector>

namespace blueward {

namespace {

class Hasher {
public:
    Hasher() {
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return;
        algorithm_ = algorithm;
        DWORD objectLength = 0, got = 0;
        if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength), sizeof objectLength, &got, 0) != 0 ||
            BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&digestLength_), sizeof digestLength_, &got, 0) != 0)
            return;
        object_.resize(objectLength);
        BCRYPT_HASH_HANDLE hash = nullptr;
        if (BCryptCreateHash(algorithm, &hash, object_.data(), objectLength, nullptr, 0, 0) == 0) hash_ = hash;
    }
    ~Hasher() {
        if (hash_) BCryptDestroyHash(hash_);
        if (algorithm_) BCryptCloseAlgorithmProvider(algorithm_, 0);
    }
    Hasher(const Hasher&) = delete;
    Hasher& operator=(const Hasher&) = delete;

    bool ok() const { return hash_ != nullptr; }

    void Update(const void* data, std::size_t size) {
        if (!hash_) return;
        const auto* bytes = static_cast<const unsigned char*>(data);
        while (size > 0) {
            const ULONG chunk = size > (1u << 30) ? (1u << 30) : static_cast<ULONG>(size);
            BCryptHashData(hash_, const_cast<PUCHAR>(bytes), chunk, 0);
            bytes += chunk;
            size -= chunk;
        }
    }

    std::string FinishHex() {
        if (!hash_) return {};
        std::vector<unsigned char> digest(digestLength_);
        const NTSTATUS status = BCryptFinishHash(hash_, digest.data(), digestLength_, 0);
        BCryptDestroyHash(hash_);
        hash_ = nullptr;
        if (status != 0) return {};
        static const char* const kHex = "0123456789abcdef";
        std::string out;
        out.reserve(digest.size() * 2);
        for (const unsigned char byte : digest) {
            out.push_back(kHex[byte >> 4]);
            out.push_back(kHex[byte & 15]);
        }
        return out;
    }

private:
    BCRYPT_ALG_HANDLE algorithm_ = nullptr;
    BCRYPT_HASH_HANDLE hash_ = nullptr;
    DWORD digestLength_ = 0;
    std::vector<unsigned char> object_;
};

}  // namespace

bool Sha256Bytes(const void* data, std::size_t size, std::string& hex) {
    hex.clear();
    Hasher hasher;
    if (!hasher.ok()) return false;
    hasher.Update(data, size);
    hex = hasher.FinishHex();
    return hex.size() == 64;
}

bool Sha256String(std::string_view text, std::string& hex) { return Sha256Bytes(text.data(), text.size(), hex); }

bool Sha256File(const std::wstring& path, std::string& hex, const std::function<bool(std::uint64_t, std::uint64_t)>& progress,
                std::string& error) {
    hex.clear();
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = "cannot open " + std::to_string(GetLastError());
        return false;
    }
    LARGE_INTEGER size{};
    GetFileSizeEx(file, &size);
    Hasher hasher;
    if (!hasher.ok()) {
        CloseHandle(file);
        error = "cannot start SHA-256";
        return false;
    }
    std::vector<unsigned char> buffer(1u << 20);
    std::uint64_t done = 0;
    bool ok = true;
    for (;;) {
        DWORD got = 0;
        if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &got, nullptr)) {
            error = "cannot read " + std::to_string(GetLastError());
            ok = false;
            break;
        }
        if (got == 0) break;
        hasher.Update(buffer.data(), got);
        done += got;
        if (progress && !progress(done, static_cast<std::uint64_t>(size.QuadPart))) {
            error = "cancelled";
            ok = false;
            break;
        }
    }
    CloseHandle(file);
    if (!ok) return false;
    hex = hasher.FinishHex();
    if (hex.size() != 64) {
        error = "SHA-256 failed";
        return false;
    }
    return true;
}

bool Sha256File(const std::wstring& path, std::string& hex) {
    std::string error;
    return Sha256File(path, hex, nullptr, error);
}

bool IsSha256Hex(std::string_view value) {
    if (value.size() != 64) return false;
    for (const char c : value)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

std::string LowerHex(std::string_view value) {
    std::string out(value);
    for (char& c : out)
        if (c >= 'A' && c <= 'F') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

}  // namespace blueward
