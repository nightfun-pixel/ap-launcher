#include "check.h"
#include "helpers.h"
#include "zipwrite.h"

#include <string>
#include <vector>

#include "core/zip.h"

namespace {

std::vector<std::string> Five() { return {"vstring.bytes", "table_data.bytes", "static-text.r", "startup.r", "onest-font.r"}; }

testzip::Item Item(const std::string& name, const std::string& data, bool deflate = false) {
    testzip::Item item;
    item.name = name;
    item.data = data;
    item.deflate = deflate;
    return item;
}

bool Extract(const std::wstring& zip, const std::vector<std::string>& allowed, const std::wstring& dest, std::string& error,
             std::string* code = nullptr) {
    return blueward::ExtractAllowed(zip, allowed, dest, error, code);
}

}  // namespace

void RunZipTests() {
    {
        // T-079: five entries, stored and deflated, exact bytes.
        const std::wstring zip = testutil::Path(L"zip-ok.zip");
        std::vector<testzip::Item> items;
        for (std::size_t i = 0; i < Five().size(); ++i) items.push_back(Item(Five()[i], std::string(1000 + i * 111, static_cast<char>('a' + i)), i % 2 == 0));
        std::string error;
        CHECK(testzip::Write(zip, items, error));
        const std::wstring dest = testutil::Path(L"zip-out");
        std::string code;
        CHECK(Extract(zip, Five(), dest, error, &code));
        CHECK(error.empty());
        CHECK(code.empty());
        for (std::size_t i = 0; i < Five().size(); ++i) CHECK(testutil::Read(dest + L"\\" + blueward::Widen(Five()[i])) == items[i].data);
        CHECK(Extract(zip, Five(), dest, error));
    }
    {
        // T-080: a validated sixth entry is ignored, seven is too many, four is incomplete.
        const std::wstring zip6 = testutil::Path(L"zip-six.zip");
        std::vector<testzip::Item> six;
        for (const std::string& name : Five()) six.push_back(Item(name, "data"));
        six.push_back(Item("readme.txt", "extra"));
        std::string error;
        CHECK(testzip::Write(zip6, six, error));
        const std::wstring out6 = testutil::Path(L"zip-six-out");
        CHECK(Extract(zip6, Five(), out6, error));
        CHECK(!testutil::Exists(out6 + L"\\readme.txt"));

        const std::wstring zip7 = testutil::Path(L"zip-seven.zip");
        std::vector<testzip::Item> seven = six;
        seven.push_back(Item("extra.txt", "extra"));
        CHECK(testzip::Write(zip7, seven, error));
        std::string code;
        CHECK(!Extract(zip7, Five(), testutil::Path(L"zip-seven-out"), error, &code));
        CHECK(code == "E_ZIP_COUNT");

        const std::wstring zip4 = testutil::Path(L"zip-four.zip");
        std::vector<testzip::Item> four;
        for (std::size_t i = 0; i + 1 < Five().size(); ++i) four.push_back(Item(Five()[i], "data"));
        CHECK(testzip::Write(zip4, four, error));
        code.clear();
        CHECK(!Extract(zip4, Five(), testutil::Path(L"zip-four-out"), error, &code));
        CHECK(code == "E_ZIP_MISSING");

        const std::wstring zip0 = testutil::Path(L"zip-zero.zip");
        CHECK(testzip::Write(zip0, {}, error));
        code.clear();
        CHECK(!Extract(zip0, Five(), testutil::Path(L"zip-zero-out"), error, &code));
        CHECK(code == "E_ZIP_COUNT");
    }
    {
        // T-081: traversal, absolute, drive, backslash, percent, dot, Unicode and control names.
        const char* evilNames[] = {"../evil.txt", "/abs.txt", "C:/evil.txt", "a\\b.txt", "%2e%2e.txt", "..", "a/b.txt",
                                   "vstring.bytes\x01", "имя.txt"};
        for (const char* name : evilNames) {
            const std::wstring zip = testutil::Path(L"zip-evil.zip");
            std::string error;
            CHECK(testzip::Write(zip, {Item(name, "x")}, error));
            std::string code;
            CHECK_MSG(!Extract(zip, {name}, testutil::Path(L"zip-evil-out"), error, &code), name);
            CHECK(code == "E_ZIP_NAME" || code == "E_ZIP_TYPE");
            CHECK(!testutil::Exists(testutil::Path(L"zip-evil-out\\evil.txt")));
        }
    }
    {
        // T-082: directory and symlink entries are refused.
        const std::wstring zipDir = testutil::Path(L"zip-dir.zip");
        std::string error;
        testzip::Item directory = Item("folder/", "");
        directory.directory = true;
        CHECK(testzip::Write(zipDir, {directory}, error));
        blueward::ZipReader reader;
        std::string code;
        CHECK(!reader.Open(zipDir, error, &code));
        CHECK(code == "E_ZIP_TYPE");
        const std::wstring zipLink = testutil::Path(L"zip-link.zip");
        testzip::Item link = Item("link.bin", "../target");
        link.symlink = true;
        CHECK(testzip::Write(zipLink, {link}, error));
        code.clear();
        CHECK(!reader.Open(zipLink, error, &code));
        CHECK(code == "E_ZIP_TYPE");
    }
    {
        // T-083: encryption, unsupported method, ZIP64, truncation.
        const std::wstring zipEncrypted = testutil::Path(L"zip-encrypted.zip");
        std::string error;
        testzip::Item encrypted = Item("a.bin", "data");
        encrypted.encrypted = true;
        CHECK(testzip::Write(zipEncrypted, {encrypted}, error));
        blueward::ZipReader reader;
        std::string code;
        CHECK(!reader.Open(zipEncrypted, error, &code));
        CHECK(code == "E_ZIP_OPEN");

        const std::wstring zipZip64 = testutil::Path(L"zip-zip64.zip");
        testzip::Item zip64 = Item("a.bin", "data");
        zip64.zip64Size = true;
        CHECK(testzip::Write(zipZip64, {zip64}, error));
        code.clear();
        CHECK(!reader.Open(zipZip64, error, &code));
        CHECK(code == "E_ZIP_OPEN");

        const std::wstring zipTruncated = testutil::Path(L"zip-truncated.zip");
        CHECK(testzip::Write(zipTruncated, {Item("a.bin", "data")}, error));
        CHECK(testzip::Truncate(zipTruncated, 12));
        code.clear();
        CHECK(!reader.Open(zipTruncated, error, &code));
        CHECK(code == "E_ZIP_OPEN");

        const std::wstring zipCrc = testutil::Path(L"zip-crc.zip");
        testzip::Item bad = Item("a.bin", "data", true);
        bad.badCrc = true;
        CHECK(testzip::Write(zipCrc, {bad}, error));
        code.clear();
        CHECK(!Extract(zipCrc, {"a.bin"}, testutil::Path(L"zip-crc-out"), error, &code));
        CHECK(code == "E_ZIP_CRC");
    }
    {
        // T-084: case-insensitive duplicate names are refused.
        const std::wstring zip = testutil::Path(L"zip-duplicate.zip");
        std::string error;
        CHECK(testzip::Write(zip, {Item("vstring.bytes", "a"), Item("VSTRING.BYTES", "b")}, error));
        blueward::ZipReader reader;
        std::string code;
        CHECK(!reader.Open(zip, error, &code));
        CHECK(code == "E_ZIP_DUPLICATE");
    }
    {
        CHECK(!blueward::IsSafeEntryName(""));
        CHECK(!blueward::IsSafeEntryName("../a"));
        CHECK(!blueward::IsSafeEntryName("a\\b"));
        CHECK(!blueward::IsSafeEntryName("C:/a"));
        CHECK(!blueward::IsSafeEntryName("/a"));
        CHECK(!blueward::IsSafeEntryName("a/"));
        CHECK(blueward::IsSafeEntryName("vstring.bytes"));
        CHECK(blueward::IsSafeEntryName("readme.txt"));
    }
}
