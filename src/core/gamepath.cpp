#include "core/gamepath.h"

#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cwchar>
#include <set>

#include "core/fileio.h"
#include "core/json.h"
#include "core/log.h"
#include "core/manifest.h"
#include "core/text.h"

namespace blueward {

namespace {

std::wstring Trim(std::wstring value) {
    while (!value.empty() && (value.front() == L' ' || value.front() == L'\t' || value.front() == L'"')) value.erase(value.begin());
    while (!value.empty() && (value.back() == L' ' || value.back() == L'\t' || value.back() == L'"')) value.pop_back();
    return value;
}

bool IsDigits(const std::wstring& value) {
    if (value.empty() || value.size() > 12) return false;
    for (const wchar_t c : value)
        if (c < L'0' || c > L'9') return false;
    return true;
}

bool IsAbsoluteLocal(const std::wstring& path) {
    if (path.size() < 3 || path[1] != L':' || (path[2] != L'\\' && path[2] != L'/')) return false;
    const wchar_t letter = static_cast<wchar_t>(towupper(path[0]));
    if (letter < L'A' || letter > L'Z') return false;
    const std::wstring root = std::wstring(1, letter) + L":\\";
    return GetDriveTypeW(root.c_str()) == DRIVE_FIXED;
}

bool HasReparse(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

// GR itself and every component below it must be free of reparse points (FR-76).
bool PathComponentsClear(const std::wstring& gameDir) {
    if (HasReparse(gameDir)) return false;
    const wchar_t* tail[] = {kDataDir, L"StreamingAssets", L".res"};
    std::wstring path = gameDir;
    for (const wchar_t* part : tail) {
        path += L"\\";
        path += part;
        if (DirExists(path) && HasReparse(path)) return false;
    }
    return true;
}

bool ValidGameRoot(const std::wstring& dir) {
    if (dir.empty()) return false;
    if (dir.rfind(L"\\\\", 0) == 0) return false;  // UNC or device path
    if (!IsAbsoluteLocal(dir)) return false;
    if (!FileExists(dir + L"\\" + kGameExe)) return false;
    if (!DirExists(dir + L"\\" + kDataDir)) return false;
    if (!DirExists(dir + L"\\" + kDataDir + L"\\" + kResDir)) return false;
    return PathComponentsClear(dir);
}

std::wstring RevisionFromVersionFile(const std::wstring& name) {
    const std::wstring suffix = L".version";
    if (name.size() <= suffix.size()) return {};
    std::wstring stem = name.substr(0, name.size() - suffix.size());
    const std::size_t underscore = stem.find_last_of(L'_');
    if (underscore == std::wstring::npos) return {};
    const std::wstring revision = stem.substr(underscore + 1);
    return IsDigits(revision) ? revision : std::wstring();
}

struct PackageVersion {
    bool found = false;
    bool valid = true;
    std::wstring value;
};

PackageVersion ReadPackageVersion(const std::wstring& resRoot, const wchar_t* package) {
    PackageVersion result;
    const std::wstring dir = resRoot + L"\\" + package;
    if (!DirExists(dir)) return result;
    std::vector<std::wstring> files;
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW((dir + L"\\*.version").c_str(), &data);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) continue;
            files.push_back(dir + L"\\" + data.cFileName);
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    if (files.size() != 1) {
        result.found = !files.empty();
        result.valid = false;
        return result;
    }
    result.found = true;
    std::string bytes;
    if (!ReadFileBytes(files[0], bytes, 32)) {
        result.valid = false;
        return result;
    }
    while (!bytes.empty() && (bytes.back() == '\r' || bytes.back() == '\n' || bytes.back() == ' ' || bytes.back() == '\t')) bytes.pop_back();
    const std::wstring content = Widen(bytes);
    if (!IsDigits(content)) {
        result.valid = false;
        return result;
    }
    result.value = content;
    const std::wstring nameRevision = RevisionFromVersionFile(BaseName(files[0]));
    if (!nameRevision.empty() && nameRevision != content) {
        result.valid = false;
        result.value.clear();
    }
    return result;
}

std::wstring HeadVersion(const std::wstring& gameDir) {
    std::string bytes;
    if (ReadFileBytes(gameDir + L"\\game-launcher-config.json", bytes, 64u << 10)) {
        Json document;
        std::string error;
        if (ParseJson(bytes, document, error)) {
            const Json* version = document.Find("version");
            if (version && version->isString() && IsNumericVersion(version->asString())) return Widen(version->asString());
        }
    }
    // Bounded fallback: the first 64 KiB of the 8 MB launcher manifest, not a full parse.
    std::string head;
    if (!ReadFileBytes(gameDir + L"\\manifest.json", head, 64u << 10)) return {};
    const std::string needle = "\"version\"";
    std::size_t at = head.find(needle);
    while (at != std::string::npos) {
        std::size_t i = at + needle.size();
        while (i < head.size() && (head[i] == ' ' || head[i] == '\t' || head[i] == '\r' || head[i] == '\n')) ++i;
        if (i < head.size() && head[i] == ':') {
            ++i;
            while (i < head.size() && (head[i] == ' ' || head[i] == '\t' || head[i] == '\r' || head[i] == '\n')) ++i;
            if (i < head.size() && head[i] == '"') {
                const std::size_t end = head.find('"', i + 1);
                if (end != std::string::npos) {
                    const std::string value = head.substr(i + 1, end - i - 1);
                    if (IsNumericVersion(value)) return Widen(value);
                }
            }
        }
        at = head.find(needle, at + 1);
    }
    return {};
}

}  // namespace

bool IsGameDir(const std::wstring& dir) { return ValidGameRoot(dir); }

std::wstring ResRoot(const std::wstring& gameDir) { return gameDir + L"\\" + kDataDir + L"\\" + kResDir; }

std::wstring NormalizeGameDir(const std::wstring& chosen) {
    std::wstring path = Trim(chosen);
    if (path.empty()) return {};
    if (path.rfind(L"\\\\", 0) == 0) return {};
    std::replace(path.begin(), path.end(), L'/', L'\\');
    while (path.size() > 3 && path.back() == L'\\') path.pop_back();
    if (path.size() == 2 && path[1] == L':') path += L'\\';
    if (FileExists(path) && _wcsicmp(BaseName(path).c_str(), kGameExe) == 0) path = ParentDir(path);
    std::vector<std::wstring> candidates;
    candidates.push_back(path);
    candidates.push_back(path + L"\\AZUPRO_JP");
    const std::wstring parent = ParentDir(path);
    if (!parent.empty()) {
        candidates.push_back(parent);
        candidates.push_back(parent + L"\\AZUPRO_JP");
    }
    if (_wcsicmp(BaseName(path).c_str(), kDataDir) == 0) candidates.push_back(ParentDir(path));
    for (const std::wstring& candidate : candidates) {
        std::wstring normal = candidate;
        while (normal.size() > 3 && normal.back() == L'\\') normal.pop_back();
        if (ValidGameRoot(normal)) return normal;
    }
    return {};
}

std::wstring ReadGameVersion(const std::wstring& gameDir) { return HeadVersion(gameDir); }

bool ReadResVersions(const std::wstring& resRoot, std::map<std::wstring, std::wstring>& versions) {
    versions.clear();
    if (!DirExists(resRoot)) return false;
    for (const wchar_t* package : {L"default_package", L"raw_base_package", L"raw_streaming_package", L"static_package"}) {
        const PackageVersion version = ReadPackageVersion(resRoot, package);
        if (version.found && version.valid) versions[package] = version.value;
    }
    return !versions.empty();
}

std::wstring LocalResourceRevision(const std::wstring& gameDir) {
    const std::wstring rootB = ResRoot(gameDir);
    const std::wstring rootA = gameDir + L"\\" + kDataDir + L"\\.res";
    for (const wchar_t* package : {L"default_package", L"raw_base_package"}) {
        const PackageVersion b = ReadPackageVersion(rootB, package);
        const PackageVersion a = ReadPackageVersion(rootA, package);
        if ((b.found && !b.valid) || (a.found && !a.valid)) return {};
        if (b.found && a.found && b.value != a.value) return {};
        const std::wstring value = b.found ? b.value : a.value;
        if (value.empty()) continue;
        const std::wstring& sourceRoot = b.found ? rootB : rootA;
        const PackageVersion streaming = ReadPackageVersion(sourceRoot, L"raw_streaming_package");
        if (streaming.found && (!streaming.valid || streaming.value != value)) return {};
        return value;
    }
    return {};
}

bool IsProcessRunning(const std::wstring& exeName) {
    if (exeName.empty()) return false;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof entry;
    bool found = false;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, exeName.c_str()) == 0) {
                found = true;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return found;
}

bool GameProcessesRunning(const std::function<bool(const std::wstring&)>& probe) {
    if (!probe) return false;
    return probe(kGameExe) || probe(kGameLauncherExe);
}

std::vector<std::wstring> CommonGameRoots(const std::wstring& exeDir) {
    std::vector<std::wstring> roots;
    auto add = [&](const std::wstring& root) {
        if (root.empty()) return;
        if (roots.size() >= 128) return;
        if (std::find(roots.begin(), roots.end(), root) == roots.end()) roots.push_back(root);
    };
    wchar_t buffer[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"ProgramFiles", buffer, MAX_PATH) > 0) add(std::wstring(buffer) + L"\\YostarGames\\AZUPRO_JP");
    if (GetEnvironmentVariableW(L"ProgramFiles(x86)", buffer, MAX_PATH) > 0) add(std::wstring(buffer) + L"\\YostarGames\\AZUPRO_JP");
    const DWORD drives = GetLogicalDrives();
    for (int i = 0; i < 26 && roots.size() < 128; ++i) {
        if ((drives & (1u << i)) == 0) continue;
        const wchar_t letter = static_cast<wchar_t>(L'A' + i);
        const std::wstring drive = std::wstring(1, letter) + L":\\";
        if (GetDriveTypeW(drive.c_str()) != DRIVE_FIXED) continue;
        add(drive + L"YostarGames\\AZUPRO_JP");
        add(drive + L"Games\\YostarGames\\AZUPRO_JP");
        add(drive + L"AZUPRO_JP");
    }
    (void)exeDir;
    return roots;
}

std::wstring AutoDetectGameDir(const std::vector<std::wstring>& roots) {
    int probes = 0;
    for (const std::wstring& root : roots) {
        if (++probes > 128) break;
        if (ValidGameRoot(root)) return root;
        const std::wstring child = root + L"\\AZUPRO_JP";
        if (++probes > 128) break;
        if (ValidGameRoot(child)) return child;
    }
    return {};
}

}  // namespace blueward
