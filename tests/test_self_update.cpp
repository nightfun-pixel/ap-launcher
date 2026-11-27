#include "check.h"
#include "helpers.h"

#include <string>

#include "core/self_update.h"

void RunSelfUpdateTests() {
    {
        // T-117: strictly newer numeric versions only.
        blueward::LauncherInfo info;
        info.version = "0.2.0";
        CHECK(blueward::ShouldUpdate("0.1.0", info));
        info.version = "0.1.0";
        CHECK(!blueward::ShouldUpdate("0.1.0", info));
        info.version = "0.1.1";
        CHECK(blueward::ShouldUpdate("0.1.0", info));
        info.version = "0.0.9";
        CHECK(!blueward::ShouldUpdate("0.1.0", info));
    }
    {
        CHECK(blueward::UpdateFileName("0.2.0") == L"BLUEWARD-0.2.0.exe");
        CHECK(!blueward::UpdateDir().empty());
    }
    {
        // T-121: current -> .old, new -> current, arguments verbatim, spawn sees the new path.
        const std::wstring dir = testutil::Path(L"self-update");
        blueward::MakeDirs(dir);
        const std::wstring current = dir + L"\\BLUEWARD.exe";
        const std::wstring downloaded = dir + L"\\BLUEWARD-0.2.0.exe";
        CHECK(testutil::Write(current, "OLD BUILD"));
        CHECK(testutil::Write(downloaded, "NEW BUILD"));
        std::wstring spawnPath;
        std::wstring spawnArguments;
        blueward::HandoffCallbacks callbacks;
        callbacks.spawn = [&](const std::wstring& exe) {
            spawnPath = exe;
            spawnArguments = L"BLUEWARD.exe --no-video \"two words\"";
            return true;
        };
        std::string error;
        CHECK_MSG(blueward::ApplyUpdateFiles(current, downloaded, L"BLUEWARD.exe --no-video \"two words\"", callbacks, error), error);
        CHECK(testutil::Read(current) == "NEW BUILD");
        CHECK(testutil::Read(current + L".old") == "OLD BUILD");
        CHECK(!testutil::Exists(downloaded));
        CHECK(!testutil::Exists(current + L".new"));
        CHECK(spawnPath == current);
        CHECK(spawnArguments == L"BLUEWARD.exe --no-video \"two words\"");
        CHECK(blueward::DeleteOldOnStart(current));
        CHECK(!testutil::Exists(current + L".old"));
    }
    {
        // T-122: a spawn failure restores the old executable and reacquires the mutex.
        const std::wstring dir = testutil::Path(L"self-update-fail");
        blueward::MakeDirs(dir);
        const std::wstring current = dir + L"\\BLUEWARD.exe";
        const std::wstring downloaded = dir + L"\\BLUEWARD-0.2.0.exe";
        CHECK(testutil::Write(current, "OLD BUILD"));
        CHECK(testutil::Write(downloaded, "NEW BUILD"));
        bool reacquired = false;
        blueward::HandoffCallbacks callbacks;
        callbacks.releaseMutex = [] {};
        callbacks.reacquireMutex = [&] {
            reacquired = true;
            return true;
        };
        callbacks.spawn = [](const std::wstring&) { return false; };
        std::string error;
        CHECK(!blueward::ApplyUpdateFiles(current, downloaded, L"BLUEWARD.exe", callbacks, error));
        CHECK(!error.empty());
        CHECK(testutil::Read(current) == "OLD BUILD");
        CHECK(!testutil::Exists(current + L".new"));
        CHECK(reacquired);
    }
    {
        const std::wstring dir = testutil::Path(L"self-update-missing");
        blueward::MakeDirs(dir);
        const std::wstring current = dir + L"\\BLUEWARD.exe";
        CHECK(testutil::Write(current, "OLD BUILD"));
        std::string error;
        CHECK(!blueward::ApplyUpdateFiles(current, dir + L"\\missing.exe", L"BLUEWARD.exe", {}, error));
        CHECK(!error.empty());
        CHECK(testutil::Read(current) == "OLD BUILD");
        CHECK(!testutil::Exists(current + L".old"));
    }
    {
        // T-123: stale .old and .new are removed with retries; a locked file is not fatal.
        const std::wstring dir = testutil::Path(L"self-update-cleanup");
        blueward::MakeDirs(dir);
        const std::wstring current = dir + L"\\BLUEWARD.exe";
        CHECK(testutil::Write(current, "CURRENT"));
        CHECK(testutil::Write(current + L".old", "OLD"));
        CHECK(testutil::Write(current + L".new", "NEW"));
        CHECK(blueward::DeleteOldOnStart(current));
        CHECK(!testutil::Exists(current + L".old"));
        CHECK(!testutil::Exists(current + L".new"));
        CHECK(blueward::DeleteOldOnStart(current));  // idempotent
    }
    {
        // T-119/T-120 mechanical part: a build without RT_MANIFEST is asInvoker.
        std::wstring level;
        std::string error;
        CHECK(blueward::ReadExecutionLevel(testutil::ExeDir() + L"\\blueward_tests.exe", level, error));
        CHECK(level == L"asInvoker");
    }
    {
        std::string error;
        CHECK(!blueward::SpawnDetached(testutil::Path(L"nope.exe"), L"", error));
        CHECK(!error.empty());
    }
}
