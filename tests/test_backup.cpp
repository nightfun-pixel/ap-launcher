#include "check.h"
#include "helpers.h"

#include <string>

#include "core/backup.h"
#include "core/fileio.h"

void RunBackupTests() {
    {
        const std::wstring target = testutil::Path(L"backup-one\\target.bin");
        const std::wstring backup = testutil::Path(L"backup-one\\target.bin.apbak");
        CHECK(testutil::Write(target, "original bytes"));
        std::string error;
        bool created = false;
        CHECK(blueward::BackupOnce(target, backup, created, error));
        CHECK(created);
        CHECK(testutil::Read(backup) == "original bytes");
        bool createdAgain = true;
        CHECK(blueward::BackupOnce(target, backup, createdAgain, error));
        CHECK(!createdAgain);
        CHECK(testutil::Read(backup) == "original bytes");
        // The second call must not adopt a changed target.
        CHECK(testutil::Write(target, "changed bytes"));
        bool third = true;
        CHECK(blueward::BackupOnce(target, backup, third, error));
        CHECK(!third);
        CHECK(testutil::Read(backup) == "original bytes");
    }
    {
        const std::wstring target = testutil::Path(L"restore-one\\target.bin");
        const std::wstring backup = testutil::Path(L"restore-one\\target.bin.apbak");
        CHECK(testutil::Write(target, "patched"));
        CHECK(testutil::Write(backup, "original"));
        std::string error;
        CHECK(blueward::RestoreOne(backup, target, error));
        CHECK(testutil::Read(target) == "original");
        CHECK(testutil::Exists(backup));
    }
    {
        const std::wstring target = testutil::Path(L"restore-readonly\\target.bin");
        const std::wstring backup = testutil::Path(L"restore-readonly\\target.bin.apbak");
        CHECK(testutil::Write(target, "original"));
        CHECK(SetFileAttributesW(target.c_str(), FILE_ATTRIBUTE_READONLY) != FALSE);
        std::string error;
        CHECK(!blueward::RestoreOne(backup, target, error));  // backup missing: must fail, not throw
        CHECK(!error.empty());
        CHECK(SetFileAttributesW(target.c_str(), FILE_ATTRIBUTE_NORMAL) != FALSE);
        CHECK(testutil::Write(backup, "original"));
        CHECK(testutil::Write(target, "patched"));
        CHECK(SetFileAttributesW(target.c_str(), FILE_ATTRIBUTE_READONLY) != FALSE);
        error.clear();
        CHECK_MSG(blueward::RestoreOne(backup, target, error), error);
        CHECK(testutil::Read(target) == "original");
        const DWORD attributes = GetFileAttributesW(target.c_str());
        CHECK(attributes != INVALID_FILE_ATTRIBUTES);
        CHECK((attributes & FILE_ATTRIBUTE_READONLY) != 0);
        SetFileAttributesW(target.c_str(), FILE_ATTRIBUTE_NORMAL);
    }
    {
        const std::wstring target = testutil::Path(L"backup-missing\\nope.bin");
        const std::wstring backup = blueward::BackupPathFor(target);
        CHECK(backup == target + L".apbak");
        std::string error;
        bool created = false;
        CHECK(!blueward::BackupOnce(target, backup, created, error));
        CHECK(!error.empty());
    }
}
