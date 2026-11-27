#pragma once
// One `.apbak` beside each replaced file, never overwritten, plus atomic restore.

#include <string>

namespace blueward {

inline constexpr const wchar_t* kBackupSuffix = L".apbak";

std::wstring BackupPathFor(const std::wstring& target);

// Copies target -> backup when the backup does not exist yet. `created` is false when an existing
// backup was kept. An existing backup is never overwritten.
bool BackupOnce(const std::wstring& target, const std::wstring& backup, bool& created, std::string& error);

// Atomic backup -> target copy (sibling temporary + MoveFileEx). Keeps the backup file.
bool RestoreOne(const std::wstring& backup, const std::wstring& target, std::string& error);

}  // namespace blueward
