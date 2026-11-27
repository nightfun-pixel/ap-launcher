#include "core/fileio.h"

#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>

#include <algorithm>
#include <cwctype>
#include <fstream>

#include "core/text.h"

namespace blueward {

namespace {

void SetLastErrorText(std::string& error, const char* what, const std::wstring& path, unsigned long code) {
    error = std::string(what) + " " + Narrow(path) + " (Windows " + std::to_string(code) + ")";
}

bool CopyStream(HANDLE from, HANDLE to, std::vector<unsigned char>& buffer) {
    for (;;) {
        DWORD got = 0;
        if (!ReadFile(from, buffer.data(), static_cast<DWORD>(buffer.size()), &got, nullptr)) return false;
        if (got == 0) return true;
        DWORD put = 0;
        while (put < got) {
            DWORD written = 0;
            if (!WriteFile(to, buffer.data() + put, got - put, &written, nullptr) || written == 0) return false;
            put += written;
        }
    }
}

std::wstring TempSibling(const std::wstring& path, const wchar_t* tag) {
    return path + L"." + tag + L"-" + std::to_wstring(GetCurrentProcessId());
}

bool IsAbsolute(const std::wstring& path) {
    if (path.size() >= 2 && path[1] == L':') return true;
    if (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\') return true;
    return !path.empty() && path[0] == L'\\';
}

}  // namespace

bool MakeDirs(const std::wstring& path) {
    if (path.empty()) return false;
    const int result = SHCreateDirectoryExW(nullptr, path.c_str(), nullptr);
    return result == ERROR_SUCCESS || result == ERROR_ALREADY_EXISTS || result == ERROR_FILE_EXISTS;
}

bool FileExists(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool DirExists(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

std::uint64_t FileSize(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info)) return 0;
    return (static_cast<std::uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
}

bool ReadFileBytes(const std::wstring& path, std::string& out, std::size_t cap) {
    out.clear();
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    if (size < 0 || static_cast<std::uint64_t>(size) > cap) return false;
    file.seekg(0, std::ios::beg);
    out.resize(static_cast<std::size_t>(size));
    if (size > 0) file.read(out.data(), size);
    return file.good() || file.eof();
}

bool WriteFileAtomic(const std::wstring& path, std::string_view bytes, std::string& error) {
    const std::wstring temp = TempSibling(path, L"blueward-tmp");
    HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        SetLastErrorText(error, "cannot create", temp, GetLastError());
        return false;
    }
    bool ok = true;
    std::size_t written = 0;
    while (written < bytes.size()) {
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - written, 1u << 30));
        DWORD put = 0;
        if (!WriteFile(file, bytes.data() + written, chunk, &put, nullptr) || put != chunk) {
            SetLastErrorText(error, "cannot write", temp, GetLastError());
            ok = false;
            break;
        }
        written += put;
    }
    if (ok && !FlushFileBuffers(file)) {
        SetLastErrorText(error, "cannot flush", temp, GetLastError());
        ok = false;
    }
    CloseHandle(file);
    if (ok) {
        if (FileExists(path)) SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
        if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            SetLastErrorText(error, "cannot replace", path, GetLastError());
            ok = false;
        }
    }
    if (!ok) DeleteFileW(temp.c_str());
    return ok;
}

bool CopyFileAtomic(const std::wstring& from, const std::wstring& to, std::string& error) {
    HANDLE source = CreateFileW(from.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (source == INVALID_HANDLE_VALUE) {
        SetLastErrorText(error, "cannot open", from, GetLastError());
        return false;
    }
    WIN32_FILE_ATTRIBUTE_DATA info{};
    GetFileAttributesExW(from.c_str(), GetFileExInfoStandard, &info);
    const DWORD sourceAttributes = info.dwFileAttributes == INVALID_FILE_ATTRIBUTES ? FILE_ATTRIBUTE_NORMAL : info.dwFileAttributes;

    MakeDirs(ParentDir(to));
    const std::wstring temp = TempSibling(to, L"blueward-new");
    HANDLE target = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (target == INVALID_HANDLE_VALUE) {
        SetLastErrorText(error, "cannot create", temp, GetLastError());
        CloseHandle(source);
        return false;
    }
    std::vector<unsigned char> buffer(1u << 20);
    bool ok = CopyStream(source, target, buffer) && FlushFileBuffers(target);
    if (!ok) SetLastErrorText(error, "cannot copy", from, GetLastError());
    CloseHandle(target);
    CloseHandle(source);
    if (ok) {
        // A read-only destination cannot be replaced; its attribute is restored below.
        if (FileExists(to)) SetFileAttributesW(to.c_str(), FILE_ATTRIBUTE_NORMAL);
        if (!MoveFileExW(temp.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            SetLastErrorText(error, "cannot replace", to, GetLastError());
            ok = false;
        } else if ((sourceAttributes & FILE_ATTRIBUTE_READONLY) != 0) {
            SetFileAttributesW(to.c_str(), sourceAttributes);
        }
    }
    if (!ok) DeleteFileW(temp.c_str());
    return ok;
}

std::wstring ExePath() {
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) return {};
        if (length < buffer.size()) {
            buffer.resize(length);
            return buffer;
        }
        buffer.resize(buffer.size() * 2);
    }
}

std::wstring ExeDir() { return ParentDir(ExePath()); }

std::wstring JoinPath(const std::wstring& a, const std::wstring& b) {
    if (b.empty()) return a;
    if (IsAbsolute(b)) return b;
    if (a.empty()) return b;
    if (a.back() == L'\\' || a.back() == L'/') return a + b;
    return a + L"\\" + b;
}

std::wstring BaseName(const std::wstring& path) {
    const std::size_t at = path.find_last_of(L"\\/");
    return at == std::wstring::npos ? path : path.substr(at + 1);
}

std::wstring ParentDir(const std::wstring& path) {
    if (path.empty()) return {};
    std::wstring trimmed = path;
    while (trimmed.size() > 1 && (trimmed.back() == L'\\' || trimmed.back() == L'/')) trimmed.pop_back();
    const std::size_t at = trimmed.find_last_of(L"\\/");
    if (at == std::wstring::npos) return {};
    if (at == 2 && trimmed.size() >= 3 && trimmed[1] == L':') return trimmed.substr(0, 3);
    return trimmed.substr(0, at);
}

std::wstring FullPath(const std::wstring& path) {
    if (path.empty()) return {};
    const DWORD need = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (need == 0) return path;
    std::wstring out(need, L'\0');
    const DWORD got = GetFullPathNameW(path.c_str(), need, out.data(), nullptr);
    if (got == 0 || got >= need) return path;
    out.resize(got);
    return out;
}

bool IsSamePath(const std::wstring& a, const std::wstring& b) {
    std::wstring left = FullPath(a), right = FullPath(b);
    if (left.empty() || right.empty()) return false;
    while (!left.empty() && left.back() == L'\\') left.pop_back();
    while (!right.empty() && right.back() == L'\\') right.pop_back();
    return _wcsicmp(left.c_str(), right.c_str()) == 0;
}

bool RemoveFile(const std::wstring& path) {
    if (DeleteFileW(path.c_str())) return true;
    if (GetLastError() == ERROR_ACCESS_DENIED) {
        SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
        return DeleteFileW(path.c_str()) != FALSE;
    }
    return false;
}

bool DeleteTree(const std::wstring& dir) {
    if (!DirExists(dir)) return !FileExists(dir);
    bool ok = true;
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW((dir + L"\\*").c_str(), &data);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring name = data.cFileName;
            if (name == L"." || name == L"..") continue;
            const std::wstring child = dir + L"\\" + name;
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                if (!DeleteTree(child)) ok = false;
            } else {
                if (!RemoveFile(child)) ok = false;
            }
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    if (!RemoveDirectoryW(dir.c_str())) {
        SetFileAttributesW(dir.c_str(), FILE_ATTRIBUTE_NORMAL);
        if (!RemoveDirectoryW(dir.c_str())) ok = false;
    }
    return ok;
}

bool SafeJoin(const std::wstring& root, const std::string& relativeSlashPath, std::wstring& out) {
    if (relativeSlashPath.empty()) return false;
    if (relativeSlashPath.find('\\') != std::string::npos) return false;
    if (relativeSlashPath.find(':') != std::string::npos) return false;
    if (relativeSlashPath.find('%') != std::string::npos) return false;
    if (relativeSlashPath.front() == '/') return false;
    std::wstring joined = root;
    std::size_t at = 0;
    while (at <= relativeSlashPath.size()) {
        const std::size_t next = relativeSlashPath.find('/', at);
        const std::string part = relativeSlashPath.substr(at, next == std::string::npos ? std::string::npos : next - at);
        if (part.empty() || part == "." || part == "..") return false;
        joined += L"\\" + Widen(part);
        if (next == std::string::npos) break;
        at = next + 1;
    }
    const std::wstring canonicalRoot = FullPath(root);
    const std::wstring canonical = FullPath(joined);
    std::wstring prefix = canonicalRoot;
    while (!prefix.empty() && prefix.back() == L'\\') prefix.pop_back();
    if (canonical.size() <= prefix.size()) return false;
    if (_wcsnicmp(canonical.c_str(), prefix.c_str(), prefix.size()) != 0) return false;
    if (canonical[prefix.size()] != L'\\') return false;
    out = canonical;
    return true;
}

std::vector<std::wstring> ListFiles(const std::wstring& dir) {
    std::vector<std::wstring> files;
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW((dir + L"\\*").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return files;
    do {
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) continue;
        files.push_back(dir + L"\\" + data.cFileName);
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return files;
}

}  // namespace blueward
