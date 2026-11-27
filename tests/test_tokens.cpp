#include "check.h"
#include "paths.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cwctype>
#include <fstream>
#include <string>
#include <vector>

#include "core/text.h"

namespace {

struct Token {
    std::string text;
    bool wordBoundary = false;
};

bool IsWordChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; }

// The tokens are assembled from fragments so this test file itself stays clean under its own scan.
// The canonical external list may be supplied in BLUEWARD_BANNED_TOKENS (semicolon/newline separated).
std::vector<Token> Banned() {
    std::vector<Token> tokens;
    const DWORD length = GetEnvironmentVariableW(L"BLUEWARD_BANNED_TOKENS", nullptr, 0);
    if (length > 1) {
        std::wstring value(length, L'\0');
        GetEnvironmentVariableW(L"BLUEWARD_BANNED_TOKENS", value.data(), length);
        std::string text = blueward::Narrow(value);
        std::string current;
        for (const char c : text) {
            if (c == ';' || c == '\n' || c == '\r') {
                if (!current.empty()) tokens.push_back({current, false});
                current.clear();
            } else {
                current.push_back(c);
            }
        }
        if (!current.empty()) tokens.push_back({current, false});
        if (!tokens.empty()) return tokens;
    }
    return {
        {std::string("night") + "wave"},
        {std::string("wu") + "wa"},
        {std::string("wuth") + "ering"},
        {std::string("ku") + "ro"},
        {std::string("con") + "vene"},        {std::string("gac") + "ha"},
        {std::string("boost") + "y"},
        {std::string("hw") + "id"},
        {std::string("zero") + " " + "trust"},
        {std::string("ops") + "client"},
        {std::string("ops") + "_key"},
        {std::string("entitle") + "ment"},
        {std::string("cloud") + "history"},
        {std::string("cloud") + "sync"},
        {std::string("pak") + "engine"},
        {std::string("pak") + "chunk"},
        {std::string("mount") + "lang"},
        {std::string("-krql") + "v"},
        {std::string("aki") + "-game"},
        {std::string("installed") + "_packs"},
        {std::string("send") + "_stats"},
        {std::string("stats") + "_endpoint"},
        {std::string("pull") + " " + "tracker"},
        {std::string(".pa") + "k"},
        {std::string("api.") + "github.com"},
        {std::string("R") + "2", true},
        {std::string("UE") + "4", true},
    };
}

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

void RunTokenTests() {
    const std::vector<Token> tokens = Banned();
    const std::string owner = std::string("night") + "fun";
    const std::string ownerUrl = "github.com/" + owner + "-pixel/ap-launcher/";
    const std::string maskedOwnerUrl = "github.com/OWNER/ap-launcher/";

    std::vector<std::wstring> files;
    Walk(BLUEWARD_SOURCE_DIR, files);
    CHECK(files.size() > 20);
    int scanned = 0;
    for (const std::wstring& path : files) {
        std::string text = ReadAll(path);
        if (text.empty()) continue;
        ++scanned;
        std::string lower = Lower(text);
        // The owner literal is allowed only inside the canonical GitHub URL prefix. Mask every
        // such occurrence, then any remaining owner token is a failure.
        std::size_t position = 0;
        while ((position = lower.find(ownerUrl, position)) != std::string::npos) {
            lower.replace(position, ownerUrl.size(), maskedOwnerUrl);
            position += maskedOwnerUrl.size();
        }
        std::string found;
        for (const Token& token : tokens) {
            std::size_t at = lower.find(token.text);
            while (at != std::string::npos) {
                bool bad = true;
                if (token.wordBoundary) {
                    const bool leftOk = at == 0 || !IsWordChar(lower[at - 1]);
                    const bool rightOk = at + token.text.size() >= lower.size() || !IsWordChar(lower[at + token.text.size()]);
                    bad = leftOk && rightOk;
                }
                if (bad) {
                    if (!found.empty()) found += ", ";
                    found += token.text;
                    break;
                }
                at = lower.find(token.text, at + 1);
            }
        }
        const std::size_t strayOwner = lower.find(owner);
        if (strayOwner != std::string::npos) {
            if (!found.empty()) found += ", ";
            found += owner;
        }
        CHECK_MSG(found.empty(), blueward::Narrow(path) + " contains: " + found);
    }
    CHECK(scanned > 20);
}
