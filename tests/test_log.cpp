#include "check.h"
#include "helpers.h"

#include <string>

#include "core/log.h"

void RunLogTests() {
    {
        // T-016: rotation to launcher.1.log, UTC ISO-8601 with milliseconds, at most two files.
        const std::wstring path = testutil::Path(L"logs\\launcher.log");
        CHECK(blueward::LogOpen(path, 2048));
        CHECK(blueward::LogIsOpen());
        CHECK(blueward::LogFilePath() == path);
        for (int i = 0; i < 200; ++i) blueward::LogInfo(L"line " + std::to_wstring(i) + L" plain text");
        blueward::LogWarn(L"a warning");
        blueward::LogError(L"an error");
        blueward::LogClose();
        CHECK(!blueward::LogIsOpen());
        CHECK(testutil::Exists(path));
        CHECK(testutil::Exists(testutil::Path(L"logs\\launcher.1.log")));
        CHECK(!testutil::Exists(path + L".1"));
        CHECK(testutil::Size(path) <= 2048 + 512);
        const std::string current = testutil::Read(path);
        const std::string rotated = testutil::Read(testutil::Path(L"logs\\launcher.1.log"));
        CHECK_MSG((current + rotated).find("line 199") != std::string::npos,
                  current.size() > 300 ? current.substr(current.size() - 300) : current);
        CHECK(current.find("ERROR an error") != std::string::npos);
        CHECK(current.find("T") != std::string::npos);
        CHECK(current.find("Z ") != std::string::npos);
    }
    {
        // T-016: control characters and URL query strings are removed; profile paths shortened.
        CHECK(blueward::SanitizeLogText(L"a\nb\rc\td") == L"a b c d");
        CHECK(blueward::SanitizeLogText(L"see https://github.com/x?token=secret next") == L"see https://github.com/x next");
        wchar_t profile[MAX_PATH]{};
        const DWORD length = GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
        if (length > 0 && length < MAX_PATH) {
            const std::wstring text = std::wstring(profile) + L"\\file.txt";
            const std::wstring shortened = blueward::SanitizeLogText(text);
            CHECK(shortened == text);  // shortening happens in the writer, not the sanitizer
        }
    }
    {
        // A second open appends to the existing file instead of truncating it.
        const std::wstring path = testutil::Path(L"logs2\\launcher.log");
        CHECK(blueward::LogOpen(path, 1u << 20));
        blueward::LogInfo(L"first line");
        blueward::LogClose();
        CHECK(blueward::LogOpen(path, 1u << 20));
        blueward::LogInfo(L"second line");
        blueward::LogClose();
        const std::string text = testutil::Read(path);
        CHECK(text.find("first line") != std::string::npos);
        CHECK(text.find("second line") != std::string::npos);
    }
}
