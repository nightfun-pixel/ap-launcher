#include "check.h"
#include "helpers.h"

#include <string>

#include "core/sha256.h"

void RunSha256Tests() {
    {
        std::string hex;
        CHECK(blueward::Sha256String("abc", hex));
        CHECK(hex == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        CHECK(blueward::IsSha256Hex(hex));
        CHECK(!blueward::IsSha256Hex("BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD"));
        CHECK(!blueward::IsSha256Hex("abc"));
        CHECK(blueward::LowerHex("ABCDEF") == "abcdef");
    }
    {
        std::string data(3u << 20, '\0');
        for (std::size_t i = 0; i < data.size(); ++i) data[i] = static_cast<char>((i * 31 + 7) & 0xFF);
        const std::wstring path = testutil::Path(L"hash-3mib.bin");
        CHECK(testutil::Write(path, data));
        std::uint64_t lastDone = 0, lastTotal = 0;
        std::string hex, error;
        CHECK(blueward::Sha256File(
            path, hex,
            [&](std::uint64_t done, std::uint64_t total) {
                lastDone = done;
                lastTotal = total;
                return true;
            },
            error));
        CHECK(error.empty());
        CHECK(lastDone == data.size());
        CHECK(lastTotal == data.size());
        std::string memoryHash;
        CHECK(blueward::Sha256Bytes(data.data(), data.size(), memoryHash));
        CHECK(hex == memoryHash);
    }
    {
        std::string hex, error;
        CHECK(!blueward::Sha256File(testutil::Path(L"missing-hash.bin"), hex, nullptr, error));
        CHECK(!error.empty());
    }
    {
        std::string hex;
        CHECK(blueward::Sha256Bytes("", 0, hex));
        CHECK(hex == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    }
}
