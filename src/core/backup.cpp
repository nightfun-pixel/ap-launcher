#include "core/backup.h"

#include <windows.h>

#include "core/fileio.h"

namespace blueward {

std::wstring BackupPathFor(const std::wstring& target) { return target + kBackupSuffix; }

bool BackupOnce(const std::wstring& target, const std::wstring& backup, bool& created, std::string& error) {
    created = false;
    if (FileExists(backup)) return true;
    if (!FileExists(target)) {
        error = "backup source does not exist";
        return false;
    }
    if (!CopyFileAtomic(target, backup, error)) return false;
    if (FileSize(backup) != FileSize(target)) {
        error = "backup size does not match the original";
        RemoveFile(backup);
        return false;
    }
    created = true;
    return true;
}

bool RestoreOne(const std::wstring& backup, const std::wstring& target, std::string& error) {
    if (!FileExists(backup)) {
        error = "backup file is missing";
        return false;
    }
    const DWORD targetAttributes = GetFileAttributesW(target.c_str());
    const bool readOnly = targetAttributes != INVALID_FILE_ATTRIBUTES && (targetAttributes & FILE_ATTRIBUTE_READONLY) != 0;
    if (!CopyFileAtomic(backup, target, error)) return false;
    if (readOnly) SetFileAttributesW(target.c_str(), FILE_ATTRIBUTE_READONLY);
    return true;
}

}  // namespace blueward
