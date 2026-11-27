#include "check.h"
#include "paths.h"

#include <windows.h>

#include <cctype>
#include <cstdio>
#include <cwctype>
#include <fstream>
#include <string>
#include <vector>

#include "core/text.h"

namespace {

bool TextFile(const std::wstring& path) {
    std::wstring lower = path;
    for (wchar_t& c : lower) c = static_cast<wchar_t>(std::towlower(c));
    const wchar_t* extensions[] = {L".cpp", L".h", L".hpp", L".rc", L".md", L".txt", L".cmd", L".json", L".in", L".iss", L".ps1", L".py"};
    for (const wchar_t* extension : extensions)
        if (lower.size() >= wcslen(extension) && lower.compare(lower.size() - wcslen(extension), wcslen(extension), extension) == 0)
            return true;
    return lower.size() >= 10 && lower.compare(lower.size() - 10, 10, L".gitignore") == 0;
}

void Walk(const std::wstring& dir, std::vector<std::wstring>& out) {
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW((dir + L"\\*").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        const std::wstring name = data.cFileName;
        if (name == L"." || name == L"..") continue;
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            if (name == L"build" || name == L"dist" || name == L".git" || name == L".vs" || name == L"out") continue;
            Walk(dir + L"\\" + name, out);
        } else if (TextFile(dir + L"\\" + name)) {
            out.push_back(dir + L"\\" + name);
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);
}

std::string ReadAll(const std::wstring& path) {
    std::ifstream file(path, std::ios::binary);
    std::string out;
    if (!file) return out;
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    file.seekg(0, std::ios::beg);
    if (size <= 0 || size > 8 * 1024 * 1024) return out;
    out.resize(static_cast<std::size_t>(size));
    file.read(out.data(), size);
    return out;
}

std::string Lower(std::string value) {
    for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

}  // namespace

void RunSourceScanTests() {
    // Releases are fetched through download URLs only; the product never calls the GitHub REST API.
    const std::string apiHost = std::string("api.") + "github" + ".com";

    std::vector<std::wstring> files;
    Walk(BLUEWARD_SOURCE_DIR, files);
    CHECK(files.size() > 20);
    int scanned = 0;
    std::string apiOffender;
    for (const std::wstring& path : files) {
        const std::string text = ReadAll(path);
        if (text.empty()) continue;
        ++scanned;
        if (Lower(text).find(apiHost) != std::string::npos) {
            apiOffender = blueward::Narrow(path);
            break;
        }
    }
    CHECK(scanned > 20);
    CHECK_MSG(apiOffender.empty(), apiOffender + " calls the GitHub API host");

    // Revisions are data from the game or the feed; none is compiled into the product.
    const char* revisions[] = {"3075915", "3219598", "3273853"};
    std::vector<std::wstring> product;
    Walk(std::wstring(BLUEWARD_SOURCE_DIR) + L"\\src", product);
    CHECK(product.size() > 20);
    std::string revisionOffender;
    std::string revisionValue;
    for (const std::wstring& path : product) {
        const std::string text = ReadAll(path);
        for (const char* revision : revisions) {
            if (text.find(revision) != std::string::npos) {
                revisionOffender = blueward::Narrow(path);
                revisionValue = revision;
                break;
            }
        }
        if (!revisionOffender.empty()) break;
    }
    CHECK_MSG(revisionOffender.empty(), revisionOffender + " hardcodes revision " + revisionValue);
}
