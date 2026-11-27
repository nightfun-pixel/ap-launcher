#pragma once
// Small file helpers shared by the installer, the config and the log. Every write that matters is
// atomic: a sibling temporary file, a flush, then MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH).

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace blueward {

bool MakeDirs(const std::wstring& path);
bool FileExists(const std::wstring& path);
bool DirExists(const std::wstring& path);
std::uint64_t FileSize(const std::wstring& path);  // 0 when the file is missing or unreadable

// Reads at most `cap` bytes. A larger file is refused instead of truncated.
bool ReadFileBytes(const std::wstring& path, std::string& out, std::size_t cap = 16u << 20);
bool WriteFileAtomic(const std::wstring& path, std::string_view bytes, std::string& error);
// Copies `from` over `to` through a sibling temporary file. Creates the parent folder of `to`.
bool CopyFileAtomic(const std::wstring& from, const std::wstring& to, std::string& error);

std::wstring ExePath();
std::wstring ExeDir();
std::wstring JoinPath(const std::wstring& a, const std::wstring& b);
std::wstring BaseName(const std::wstring& path);
std::wstring ParentDir(const std::wstring& path);
std::wstring FullPath(const std::wstring& path);
bool IsSamePath(const std::wstring& a, const std::wstring& b);

// Removes a folder tree. False when something could not be removed; the caller reports it.
bool DeleteTree(const std::wstring& dir);
bool RemoveFile(const std::wstring& path);

// Joins a forward-slash relative path to `root` and refuses anything that escapes it.
// A leading separator, a drive letter, a backslash, a colon, a percent escape or a `..`
// segment makes the call fail.
bool SafeJoin(const std::wstring& root, const std::string& relativeSlashPath, std::wstring& out);

// Lists the files directly inside `dir` (not recursive), full paths, unsorted.
std::vector<std::wstring> ListFiles(const std::wstring& dir);

}  // namespace blueward
