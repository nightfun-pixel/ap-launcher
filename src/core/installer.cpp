#include "core/installer.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <set>

#include "core/backup.h"
#include "core/errors.h"
#include "core/fileio.h"
#include "core/gamepath.h"
#include "core/json.h"
#include "core/log.h"
#include "core/messages.h"
#include "core/sha256.h"
#include "core/text.h"
#include "core/zip.h"

namespace blueward {

namespace {

constexpr int kStateSchema = 1;
constexpr int kJournalSchema = 1;
constexpr std::uint64_t kHeadroom = 64ull << 20;
constexpr int kSearchDepth = 8;
constexpr std::size_t kSearchEntries = 200000;
constexpr std::uint64_t kSearchMillis = 15000;

std::wstring Timestamp() {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t text[40];
    swprintf(text, 40, L"%04u%02u%02u-%02u%02u%02u", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    return text;
}

std::string IsoTimestamp() {
    SYSTEMTIME now{};
    GetSystemTime(&now);
    char text[32];
    snprintf(text, 32, "%04u-%02u-%02uT%02u:%02u:%02uZ", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    return text;
}

std::string LowerAscii(std::string value) {
    for (char& c : value)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return value;
}

std::wstring BaseLower(const std::wstring& path) {
    std::wstring name = BaseName(path);
    for (wchar_t& c : name) c = static_cast<wchar_t>(towlower(c));
    return name;
}

Json FileJson(const ResolvedFile& file, bool journal) {
    Json entry = Json::Object();
    entry.Set("source", Json::String(file.source));
    entry.Set("target", Json::String(file.target));
    entry.Set("resolved_target", Json::String(Narrow(file.targetPath)));
    entry.Set("resolved_via", Json::String(file.resolvedVia));
    entry.Set("before_sha256", Json::String(file.beforeSha256));
    entry.Set("before_size", Json::Int(static_cast<std::int64_t>(file.beforeSize)));
    entry.Set("sha256", Json::String(file.sha256));
    entry.Set("size", Json::Int(static_cast<std::int64_t>(file.size)));
    entry.Set("attributes", Json::Int(static_cast<std::int64_t>(file.attributes)));
    if (journal) {
        entry.Set("backup", Json::String(Narrow(file.backupPath)));
        entry.Set("backup_reused", Json::Bool(file.backupReused));
        entry.Set("committed", Json::Bool(file.committed));
    } else {
        entry.Set("installed_sha256", Json::String(file.sha256));
        entry.Set("installed_size", Json::Int(static_cast<std::int64_t>(file.size)));
    }
    return entry;
}

Json StateJson(const InstallState& state) {
    Json files = Json::Array();
    for (const ResolvedFile& file : state.files) files.Push(FileJson(file, false));
    Json document = Json::Object();
    document.Set("schema", Json::Int(kStateSchema));
    document.Set("game_root", Json::String(Narrow(state.gameRoot)));
    document.Set("patch_version", Json::String(state.patchVersion));
    document.Set("game_version", Json::String(state.gameVersion));
    document.Set("resource_revision", Json::String(state.resourceRevision));
    document.Set("manifest_sha256", Json::String(state.manifestSha256));
    document.Set("installed_at", Json::String(state.installedAt));
    document.Set("files", std::move(files));
    return document;
}

Json JournalJson(const InstallState& state, const std::string& status) {
    Json files = Json::Array();
    for (const ResolvedFile& file : state.files) files.Push(FileJson(file, true));
    Json document = Json::Object();
    document.Set("schema", Json::Int(kJournalSchema));
    document.Set("status", Json::String(status));
    document.Set("created", Json::String(IsoTimestamp()));
    document.Set("game_root", Json::String(Narrow(state.gameRoot)));
    document.Set("patch_version", Json::String(state.patchVersion));
    document.Set("game_version", Json::String(state.gameVersion));
    document.Set("resource_revision", Json::String(state.resourceRevision));
    document.Set("manifest_sha256", Json::String(state.manifestSha256));
    document.Set("files", std::move(files));
    return document;
}

bool ParseFileEntry(const Json& entry, bool journal, ResolvedFile& file, std::string& error) {
    const Json* source = entry.Find("source");
    const Json* target = entry.Find("target");
    const Json* resolved = entry.Find("resolved_target");
    const Json* via = entry.Find("resolved_via");
    const Json* beforeHash = entry.Find("before_sha256");
    const Json* beforeSize = entry.Find("before_size");
    const Json* newHash = entry.Find(journal ? "sha256" : "installed_sha256");
    const Json* newSize = entry.Find(journal ? "size" : "installed_size");
    const Json* attributes = entry.Find("attributes");
    if (!source || !source->isString() || !target || !target->isString() || !resolved || !resolved->isString() || !via ||
        !via->isString() || !beforeHash || !beforeHash->isString() || !beforeSize || !beforeSize->isInteger() || !newHash ||
        !newHash->isString() || !newSize || !newSize->isInteger()) {
        error = "a state entry is incomplete";
        return false;
    }
    file.source = source->asString();
    file.target = target->asString();
    file.targetPath = Widen(resolved->asString());
    file.resolvedVia = via->asString();
    file.beforeSha256 = beforeHash->asString();
    file.beforeSize = static_cast<std::uint64_t>(beforeSize->asInt());
    file.sha256 = newHash->asString();
    file.size = static_cast<std::uint64_t>(newSize->asInt());
    file.attributes = attributes && attributes->isInteger() ? static_cast<DWORD>(attributes->asInt()) : FILE_ATTRIBUTE_NORMAL;
    file.backupPath = BackupPathFor(file.targetPath);
    if (journal) {
        const Json* reused = entry.Find("backup_reused");
        const Json* committed = entry.Find("committed");
        file.backupReused = reused && reused->type() == Json::Type::Bool && reused->asBool();
        file.committed = committed && committed->type() == Json::Type::Bool && committed->asBool();
    }
    if (file.beforeSize == 0 || file.size == 0 || !IsSha256Hex(file.beforeSha256) || !IsSha256Hex(file.sha256)) {
        error = "a state entry has an invalid identity";
        return false;
    }
    return true;
}

bool ReadStateFile(const std::wstring& path, InstallState& state, std::string& status, std::string& error) {
    state = {};
    status.clear();
    std::string bytes;
    if (!ReadFileBytes(path, bytes, 1u << 20)) {
        error = "cannot read the state file";
        return false;
    }
    Json document;
    std::string parseError;
    if (!ParseJson(bytes, document, parseError) || !document.isObject()) {
        error = "the state file is not valid JSON: " + parseError;
        return false;
    }
    const Json* schema = document.Find("schema");
    if (!schema || !schema->isInteger() || schema->asInt() != kStateSchema) {
        error = "unsupported state schema";
        return false;
    }
    const Json* gameRoot = document.Find("game_root");
    if (!gameRoot || !gameRoot->isString()) {
        error = "the state file has no game_root";
        return false;
    }
    state.schema = kStateSchema;
    state.gameRoot = Widen(gameRoot->asString());
    const Json* patch = document.Find("patch_version");
    if (patch && patch->isString()) state.patchVersion = patch->asString();
    const Json* game = document.Find("game_version");
    if (game && game->isString()) state.gameVersion = game->asString();
    const Json* revision = document.Find("resource_revision");
    if (revision && revision->isString()) state.resourceRevision = revision->asString();
    const Json* manifestHash = document.Find("manifest_sha256");
    if (manifestHash && manifestHash->isString()) state.manifestSha256 = manifestHash->asString();
    const Json* installedAt = document.Find("installed_at");
    if (installedAt && installedAt->isString()) state.installedAt = installedAt->asString();
    const Json* statusValue = document.Find("status");
    if (statusValue && statusValue->isString()) status = statusValue->asString();
    const Json* files = document.Find("files");
    if (!files || !files->isArray() || files->items().empty()) {
        error = "the state file has no files";
        return false;
    }
    const bool journal = !status.empty();
    for (const Json& entry : files->items()) {
        ResolvedFile file;
        if (!ParseFileEntry(entry, journal, file, error)) return false;
        state.files.push_back(std::move(file));
    }
    return true;
}

bool WriteState(const std::wstring& path, const InstallState& state, std::string& error) {
    MakeDirs(ParentDir(path));
    return WriteFileAtomic(path, DumpJson(StateJson(state)), error);
}

bool WriteJournal(const std::wstring& dir, const InstallState& state, const std::string& status, std::string& error) {
    return WriteFileAtomic(dir + L"\\journal.json", DumpJson(JournalJson(state, status)), error);
}

std::wstring JournalName(const std::wstring& backupsRoot) {
    MakeDirs(backupsRoot);
    const std::wstring base = Timestamp();
    std::wstring dir = backupsRoot + L"\\" + base;
    for (int attempt = 2; DirExists(dir); ++attempt) {
        if (attempt > 100) break;
        dir = backupsRoot + L"\\" + base + L"-" + std::to_wstring(attempt);
    }
    return dir;
}

std::vector<std::wstring> JournalDirs(const std::wstring& backupsRoot) {
    std::vector<std::wstring> dirs;
    if (!DirExists(backupsRoot)) return dirs;
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW((backupsRoot + L"\\*").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return dirs;
    do {
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
        const std::wstring name = data.cFileName;
        if (name == L"." || name == L"..") continue;
        const std::wstring dir = backupsRoot + L"\\" + name;
        if (FileExists(dir + L"\\journal.json")) dirs.push_back(dir);
    } while (FindNextFileW(find, &data));
    FindClose(find);
    std::sort(dirs.begin(), dirs.end(), [](const std::wstring& a, const std::wstring& b) { return BaseName(a) > BaseName(b); });
    return dirs;
}

bool ReparseBelow(const std::wstring& gameDir, const std::wstring& path) {
    auto hasReparse = [](const std::wstring& value) {
        const DWORD attributes = GetFileAttributesW(value.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    };
    std::wstring current = gameDir;
    if (hasReparse(current)) return true;
    std::wstring relative = path.size() > gameDir.size() ? path.substr(gameDir.size()) : std::wstring();
    std::size_t at = 0;
    while (at < relative.size()) {
        while (at < relative.size() && relative[at] == L'\\') ++at;
        if (at >= relative.size()) break;
        std::size_t next = relative.find(L'\\', at);
        current += L"\\" + relative.substr(at, next == std::string::npos ? std::string::npos : next - at);
        const DWORD attributes = GetFileAttributesW(current.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) return true;
        if (next == std::string::npos) break;
        at = next;
    }
    return false;
}

bool SearchRoot(const std::wstring& root, const std::wstring& basenameLower, std::vector<std::wstring>& matches, bool& limitReached) {
    if (!DirExists(root)) return true;
    struct Item {
        std::wstring dir;
        int depth;
    };
    std::vector<Item> queue{{root, 0}};
    std::size_t visited = 0;
    const ULONGLONG started = GetTickCount64();
    for (std::size_t index = 0; index < queue.size(); ++index) {
        if (GetTickCount64() - started > kSearchMillis) {
            limitReached = true;
            return false;
        }
        const Item item = queue[index];
        WIN32_FIND_DATAW data{};
        HANDLE find = FindFirstFileW((item.dir + L"\\*").c_str(), &data);
        if (find == INVALID_HANDLE_VALUE) continue;
        do {
            const std::wstring name = data.cFileName;
            if (name == L"." || name == L"..") continue;
            if (++visited > kSearchEntries) {
                limitReached = true;
                FindClose(find);
                return false;
            }
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) continue;
            if (_wcsicmp(name.c_str(), basenameLower.c_str()) == 0 && (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
                matches.push_back(item.dir + L"\\" + name);
                continue;
            }
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 && item.depth < kSearchDepth) queue.push_back({item.dir + L"\\" + name, item.depth + 1});
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    return true;
}

std::wstring GateText(const std::wstring& revision, const std::wstring& detail) {
    return msg::RevisionUpdate(revision) + L" " + detail;
}

bool ReplaceWithNew(const std::wstring& target, const std::wstring& staged, const std::string& expectedSha, DWORD attributes,
                    std::string& error) {
    MakeDirs(ParentDir(target));
    const std::wstring temp = target + L".blueward-new";
    if (!CopyFileAtomic(staged, temp, error)) return false;
    std::string hash;
    if (!Sha256File(temp, hash) || hash != expectedSha) {
        error = "the staged copy does not match the manifest hash";
        RemoveFile(temp);
        return false;
    }
    if (FileExists(target)) SetFileAttributesW(target.c_str(), FILE_ATTRIBUTE_NORMAL);
    bool replaced = ReplaceFileW(target.c_str(), temp.c_str(), nullptr, REPLACEFILE_WRITE_THROUGH, nullptr, nullptr) != FALSE;
    if (!replaced) replaced = MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    if (!replaced) {
        error = "cannot replace the target (Windows " + std::to_string(GetLastError()) + ")";
        RemoveFile(temp);
        return false;
    }
    if (!Sha256File(target, hash) || hash != expectedSha) {
        error = "the replaced target does not match the manifest hash";
        return false;
    }
    if (attributes != INVALID_FILE_ATTRIBUTES) SetFileAttributesW(target.c_str(), attributes);
    return true;
}

bool Rollback(const InstallState& state, const std::wstring& journalDir, bool& complete, std::vector<std::wstring>& failed) {
    complete = true;
    for (auto it = state.files.rbegin(); it != state.files.rend(); ++it) {
        const ResolvedFile& file = *it;
        std::string hash;
        if (FileExists(file.targetPath) && Sha256File(file.targetPath, hash) && hash == file.beforeSha256) {
            RemoveFile(file.targetPath + L".blueward-new");
            continue;
        }
        if (!FileExists(file.backupPath) || FileSize(file.backupPath) != file.beforeSize || !Sha256File(file.backupPath, hash) ||
            hash != file.beforeSha256) {
            failed.push_back(BaseName(file.targetPath));
            complete = false;
            continue;
        }
        std::string error;
        if (!CopyFileAtomic(file.backupPath, file.targetPath, error) || !Sha256File(file.targetPath, hash) || hash != file.beforeSha256) {
            failed.push_back(BaseName(file.targetPath));
            complete = false;
            continue;
        }
        if (file.attributes != INVALID_FILE_ATTRIBUTES) SetFileAttributesW(file.targetPath.c_str(), file.attributes);
        RemoveFile(file.backupPath);
        RemoveFile(file.targetPath + L".blueward-new");
    }
    if (!journalDir.empty() && complete) DeleteTree(journalDir);
    return complete;
}

void RemoveCreatedBackups(const std::vector<std::wstring>& paths) {
    for (const std::wstring& path : paths) RemoveFile(path);
}

class GameLock {
public:
    ~GameLock() {
        if (mutex_) {
            ReleaseMutex(mutex_);
            CloseHandle(mutex_);
        }
    }
    bool Acquire(const std::wstring& gameDir, std::string& error) {
        std::wstring canonical = FullPath(gameDir);
        for (wchar_t& c : canonical) c = static_cast<wchar_t>(towlower(c));
        std::string hash;
        Sha256String(Narrow(canonical), hash);
        const std::wstring name = L"Local\\BluewardGameFiles-" + Widen(hash);
        mutex_ = CreateMutexW(nullptr, FALSE, name.c_str());
        if (!mutex_) {
            error = "cannot create the per-game mutex";
            return false;
        }
        const DWORD wait = WaitForSingleObject(mutex_, 30000);
        if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) {
            CloseHandle(mutex_);
            mutex_ = nullptr;
            error = "another BLUEWARD instance holds the game lock";
            return false;
        }
        return true;
    }

private:
    HANDLE mutex_ = nullptr;
};

bool EnoughSpace(const std::wstring& path, std::uint64_t needed) {
    ULARGE_INTEGER freeBytes{};
    if (!GetDiskFreeSpaceExW(path.c_str(), &freeBytes, nullptr, nullptr)) return true;
    return freeBytes.QuadPart >= needed;
}

std::wstring JoinErrors(const std::vector<std::wstring>& errors) {
    std::wstring message;
    for (std::size_t i = 0; i < errors.size(); ++i) {
        if (i > 0) message += L"\n";
        message += errors[i];
    }
    return message;
}

}  // namespace

std::wstring InstallStatePath(const std::wstring& stateRoot) { return stateRoot + L"\\install.json"; }

std::wstring JournalDir(const std::wstring& backupsRoot) { return JournalName(backupsRoot); }

bool HasInstallState(const std::wstring& stateRoot) { return FileExists(InstallStatePath(stateRoot)); }

bool InstalledVersion(const std::wstring& stateRoot, std::string& version, std::wstring* installedAt) {
    version.clear();
    if (!HasInstallState(stateRoot)) return false;
    InstallState state;
    std::string status, error;
    if (!ReadStateFile(InstallStatePath(stateRoot), state, status, error)) return false;
    version = state.patchVersion;
    if (installedAt) *installedAt = Widen(state.installedAt);
    return true;
}

bool BuildPlan(const Manifest& manifest, const std::wstring& gameDir, InstallPlan& plan, std::string& error) {
    plan = {};
    error.clear();
    if (!IsGameDir(gameDir)) {
        plan.errorCode = "E_GAME_INVALID";
        error = "not a valid game folder";
        return false;
    }
    plan.patchVersion = manifest.patch.version;
    plan.gameVersion = manifest.patch.gameVersion;
    plan.resourceRevision = manifest.patch.resourceRevision;
    plan.manifestSha256 = manifest.sha256;
    plan.zipSize = manifest.patch.size;
    plan.zipSha256 = manifest.patch.sha256;
    plan.gameDir = gameDir;
    plan.resRoot = ResRoot(gameDir);
    const std::wstring rootA = gameDir + L"\\" + kDataDir + L"\\.res";
    const std::wstring rootB = ResRoot(gameDir);
    for (const PatchFile& file : manifest.patch.files) {
        ResolvedFile resolved;
        resolved.source = file.source;
        resolved.target = file.target;
        resolved.beforeSha256 = file.beforeSha256;
        resolved.beforeSize = file.beforeSize;
        resolved.sha256 = file.sha256;
        resolved.size = file.size;
        if (!SafeJoin(gameDir, file.target, resolved.exactPath)) {
            plan.errorCode = "E_MANIFEST_TARGET";
            error = "target escapes the game folder: " + file.target;
            return false;
        }
        const std::wstring basename = BaseName(resolved.exactPath);
        const std::wstring basenameLower = BaseLower(resolved.exactPath);
        resolved.targetPath = resolved.exactPath;
        if (ReparseBelow(gameDir, resolved.exactPath)) {
            plan.errorCode = "E_TARGET_REPARSE";
            error = "a target component is a reparse point: " + file.target;
            return false;
        }
        if (DirExists(resolved.exactPath)) {
            plan.errorCode = "E_TARGET_NOT_FILE";
            error = "the target is a directory: " + file.target;
            return false;
        }
        if (FileExists(resolved.exactPath)) {
            resolved.resolvedVia = "exact";
        } else {
            std::vector<std::wstring> matches;
            bool limit = false;
            if (!SearchRoot(rootA, basenameLower, matches, limit) || !SearchRoot(rootB, basenameLower, matches, limit)) {
                plan.errorCode = "E_TARGET_SEARCH";
                error = "the fallback search exceeded its limit";
                return false;
            }
            if (matches.size() > 1) {
                plan.errorCode = "E_TARGET_AMBIGUOUS";
                error = "more than one file matches " + Narrow(basename);
                return false;
            }
            if (matches.size() == 1) {
                resolved.targetPath = matches[0];
                resolved.resolvedVia = matches[0].rfind(rootA, 0) == 0 ? "fallback-a" : "fallback-b";
            } else {
                resolved.missing = true;
                resolved.resolvedVia = "missing";
            }
        }
        resolved.backupPath = BackupPathFor(resolved.targetPath);
        const DWORD attributes = GetFileAttributesW(resolved.targetPath.c_str());
        resolved.attributes = attributes == INVALID_FILE_ATTRIBUTES ? FILE_ATTRIBUTE_NORMAL : attributes;
        plan.files.push_back(std::move(resolved));
    }
    if (plan.files.size() != PayloadNames().size()) {
        plan.errorCode = "E_MANIFEST_SCHEMA";
        error = "the manifest does not list the five payloads";
        return false;
    }
    return true;
}

bool CheckGameRevision(const InstallPlan& plan, std::wstring& message, std::string* code) {
    message.clear();
    if (code) code->clear();
    if (plan.resourceRevision.empty()) {
        message = msg::FromUtf8(msg::kRevisionUnknown);
        if (code) *code = "E_MANIFEST_REVISION";
        return false;
    }
    const std::wstring local = LocalResourceRevision(plan.gameDir);
    if (local.empty()) return true;  // Gate A skipped; Gate B is authoritative
    if (local != Widen(plan.resourceRevision)) {
        message = msg::RevisionUpdate(Widen(plan.resourceRevision));
        if (code) *code = "E_VERSION_REVISION";
        return false;
    }
    return true;
}

bool CheckGameVersion(const InstallPlan& plan, const std::wstring& localVersion, std::wstring& message) {
    message.clear();
    if (plan.gameVersion.empty() || localVersion.empty()) return true;
    if (Widen(plan.gameVersion) == localVersion) return true;
    message = msg::GameVersion(Widen(plan.gameVersion), localVersion);
    return false;
}

GateResult CheckBefore(const InstallPlan& plan, const std::wstring& stateRoot, std::wstring& detail, std::string* code, bool* usedManaged) {
    detail.clear();
    if (code) *code = "E_VERSION_BEFORE";
    if (usedManaged) *usedManaged = false;
    InstallState managed;
    std::string managedStatus, managedError;
    const bool managedValid = HasInstallState(stateRoot) && ReadStateFile(InstallStatePath(stateRoot), managed, managedStatus, managedError) &&
                              IsSamePath(managed.gameRoot, plan.gameDir);
    for (const ResolvedFile& file : plan.files) {
        const std::wstring name = BaseName(file.targetPath);
        if (file.missing || !FileExists(file.targetPath)) {
            detail = msg::GateDetailMissing(name);
            return GateResult::Missing;
        }
        bool reused = false;
        if (managedValid) {
            for (const ResolvedFile& entry : managed.files) {
                if (entry.target != file.target) continue;
                std::string current, backupHash;
                if (Sha256File(file.targetPath, current) && current == entry.sha256 && FileExists(file.backupPath) &&
                    FileSize(file.backupPath) == file.beforeSize && Sha256File(file.backupPath, backupHash) &&
                    backupHash == file.beforeSha256) {
                    reused = true;
                    if (usedManaged) *usedManaged = true;
                }
                break;
            }
        }
        if (reused) continue;
        if (FileSize(file.targetPath) != file.beforeSize) {
            detail = msg::GateDetailSize(name, file.beforeSize, FileSize(file.targetPath));
            return GateResult::Mismatch;
        }
        {
            std::string hash;
            if (!Sha256File(file.targetPath, hash) || hash != file.beforeSha256) {
                detail = msg::GateDetailHash(name);
                return GateResult::Mismatch;
            }
        }
    }
    if (code) code->clear();
    return GateResult::Ok;
}

bool CheckAfter(const InstallPlan& plan) {
    for (const ResolvedFile& file : plan.files) {
        std::string hash;
        if (!FileExists(file.targetPath) || FileSize(file.targetPath) != file.size || !Sha256File(file.targetPath, hash) || hash != file.sha256)
            return false;
    }
    return true;
}

bool UnpackAndVerify(const InstallPlan& plan, const std::wstring& zipPath, const std::wstring& stagingDir, std::string& error,
                     std::string* code) {
    error.clear();
    if (code) code->clear();
    std::vector<std::string> allowed;
    allowed.reserve(plan.files.size());
    for (const ResolvedFile& file : plan.files) allowed.push_back(file.source);
    if (!ExtractAllowed(zipPath, allowed, stagingDir, error)) {
        if (code) *code = "E_ZIP_EXTRACT";
        return false;
    }
    for (const ResolvedFile& file : plan.files) {
        const std::wstring path = stagingDir + L"\\" + Widen(file.source);
        if (FileSize(path) != file.size) {
            error = "payload size does not match the manifest: " + file.source;
            if (code) *code = "E_PAYLOAD_SIZE";
            return false;
        }
        std::string hash;
        if (!Sha256File(path, hash) || hash != file.sha256) {
            error = "payload SHA-256 does not match the manifest: " + file.source;
            if (code) *code = "E_PAYLOAD_HASH";
            return false;
        }
    }
    return true;
}

bool ApplyInstall(const InstallPlan& plan, const std::wstring& zipPath, const std::wstring& stagingDir, const std::wstring& stateRoot,
                  const std::wstring& backupsRoot, const InstallOptions& options, InstallReport& report, std::wstring& message) {
    report = {};
    report.patchVersion = plan.patchVersion;
    report.resourceRevision = plan.resourceRevision;
    message.clear();
    const RunningProbe running =
        options.gameRunning ? options.gameRunning : RunningProbe([] { return GameProcessesRunning(IsProcessRunning); });
    const PhaseFn phase = options.phase ? options.phase : PhaseFn([](InstallPhase) {});
    auto cancelled = [&] { return options.cancel && options.cancel->load(); };
    auto fail = [&](const char* code, std::wstring text) {
        report.code = code;
        message = std::move(text);
        return false;
    };

    phase(InstallPhase::Checking);
    if (!IsGameDir(plan.gameDir)) return fail("E_GAME_INVALID", L"Папка игры не найдена. Выберите папку с AzurPromiliaJP.exe.");
    std::wstring localMessage;
    if (!CheckGameVersion(plan, ReadGameVersion(plan.gameDir), localMessage)) return fail("E_GAME_VERSION", localMessage);
    if (!CheckGameRevision(plan, localMessage, &report.code)) {
        const std::string revisionCode = report.code;
        return fail(revisionCode.c_str(), localMessage);
    }

    if (HasInstallState(stateRoot)) {
        InstallState existing;
        std::string status, error;
        if (ReadStateFile(InstallStatePath(stateRoot), existing, status, error) && IsSamePath(existing.gameRoot, plan.gameDir) &&
            CheckAfter(plan)) {
            report.alreadyCurrent = true;
            report.installed = static_cast<int>(plan.files.size());
            message = msg::FromUtf8(msg::kAlreadyInstalled);
            phase(InstallPhase::Done);
            return true;
        }
    }

    auto failsRunning = [&](std::wstring& text, std::string& code) {
        if (options.gameRunning) {
            // An injected probe (tests) reports "the game", never a real process on this machine.
            text = msg::FromUtf8(msg::kGameRunning);
            code = "E_GAME_RUNNING";
            return false;
        }
        const bool launcher = IsProcessRunning(kGameLauncherExe);
        text = msg::FromUtf8(launcher ? msg::kLauncherRunning : msg::kGameRunning);
        code = launcher ? "E_LAUNCHER_RUNNING" : "E_GAME_RUNNING";
        return false;
    };

    std::wstring detail;
    const GateResult gate = CheckBefore(plan, stateRoot, detail, &report.code);
    if (gate != GateResult::Ok) return fail("E_VERSION_BEFORE", GateText(Widen(plan.resourceRevision), detail));
    if (running()) {
        failsRunning(message, report.code);
        return false;
    }
    if (FileSize(zipPath) != plan.zipSize) return fail("E_DOWNLOAD_SIZE", L"Архив перевода повреждён: размер не совпадает с манифестом.");
    {
        std::string zipHash;
        if (!Sha256File(zipPath, zipHash) || zipHash != plan.zipSha256)
            return fail("E_DOWNLOAD_HASH", L"Архив перевода повреждён: SHA-256 не совпадает с манифестом.");
    }
    if (!CheckGameRevision(plan, localMessage, &report.code)) {
        const std::string revisionCode = report.code;
        return fail(revisionCode.c_str(), localMessage);
    }
    if (cancelled()) return fail("", L"Установка отменена.");

    std::uint64_t payloadSum = 0;
    for (const ResolvedFile& file : plan.files) payloadSum += file.size;
    if (!EnoughSpace(plan.gameDir, payloadSum + kHeadroom) ||
        (!IsSamePath(stateRoot, plan.gameDir) && !EnoughSpace(stateRoot, plan.zipSize + payloadSum + kHeadroom)))
        return fail("E_DISK_SPACE", L"Недостаточно места на диске: нужно " +
                                      std::to_wstring((payloadSum + kHeadroom) >> 20) + L" МиБ для файлов игры и " +
                                      std::to_wstring((plan.zipSize + payloadSum + kHeadroom) >> 20) + L" МиБ для состояния.");

    GameLock gameLock;
    std::string lockError;
    if (!gameLock.Acquire(plan.gameDir, lockError)) return fail("E_INSTANCE_BUSY", L"Другая копия BLUEWARD уже устанавливает перевод. Подождите.");

    phase(InstallPhase::Extracting);
    {
        std::string unpackError;
        if (!UnpackAndVerify(plan, zipPath, stagingDir, unpackError, &report.code)) {
            const std::string unpackCode = report.code;
            DeleteTree(stagingDir);
            return fail(unpackCode.c_str(), L"Не удалось распаковать архив: " + Widen(unpackError));
        }
    }
    if (cancelled()) {
        DeleteTree(stagingDir);
        return fail("", L"Установка отменена.");
    }
    if (running()) {
        DeleteTree(stagingDir);
        RemoveFile(zipPath);
        failsRunning(message, report.code);
        return false;
    }
    // The tree may have changed during download. Re-run the managed gate before any backup.
    {
        std::wstring changedDetail;
        if (CheckBefore(plan, stateRoot, changedDetail, &report.code) != GateResult::Ok) {
            DeleteTree(stagingDir);
            RemoveFile(zipPath);
            std::wstring changedName;
            for (const ResolvedFile& file : plan.files) {
                std::string hash;
                if (!FileExists(file.targetPath) || FileSize(file.targetPath) != file.beforeSize || !Sha256File(file.targetPath, hash) ||
                    hash != file.beforeSha256) {
                    changedName = BaseName(file.targetPath);
                    break;
                }
            }
            return fail("E_VERSION_BEFORE", GateText(Widen(plan.resourceRevision), msg::GateDetailChanged(changedName)));
        }
    }

    phase(InstallPhase::Installing);
    InstallState state;
    state.gameRoot = plan.gameDir;
    state.patchVersion = plan.patchVersion;
    state.gameVersion = plan.gameVersion;
    state.resourceRevision = plan.resourceRevision;
    state.manifestSha256 = plan.manifestSha256;
    state.installedAt = IsoTimestamp();
    state.files = plan.files;

    const std::wstring journalDir = JournalName(backupsRoot);
    if (!MakeDirs(journalDir)) return fail("E_STATE_WRITE", L"Не удалось создать папку журнала установки.");
    std::string writeError;
    if (!WriteJournal(journalDir, state, "backing_up", writeError)) {
        DeleteTree(journalDir);
        return fail("E_STATE_WRITE", L"Не удалось записать журнал установки: " + Widen(writeError));
    }
    report.journalDir = journalDir;

    std::vector<std::wstring> createdBackups;
    for (ResolvedFile& file : state.files) {
        if (FileExists(file.backupPath)) {
            std::string backupHash;
            if (FileSize(file.backupPath) != file.beforeSize || !Sha256File(file.backupPath, backupHash) ||
                backupHash != file.beforeSha256) {
                bool complete = false;
                std::vector<std::wstring> failed;
                Rollback(state, journalDir, complete, failed);
                RemoveCreatedBackups(createdBackups);
                return fail("E_BACKUP_CONFLICT", L"Резервная копия " + BaseName(file.backupPath) + L" не совпадает с оригиналом. Удалите её и повторите.");
            }
            file.backupReused = true;
        } else {
            bool created = false;
            std::string backupError;
            if (!BackupOnce(file.targetPath, file.backupPath, created, backupError)) {
                bool complete = false;
                std::vector<std::wstring> failed;
                Rollback(state, journalDir, complete, failed);
                RemoveCreatedBackups(createdBackups);
                return fail("E_BACKUP_CREATE", L"Не удалось сохранить резервную копию " + BaseName(file.targetPath) + L": " + Widen(backupError));
            }
            if (created) createdBackups.push_back(file.backupPath);
            std::string backupHash;
            if (FileSize(file.backupPath) != file.beforeSize || !Sha256File(file.backupPath, backupHash) ||
                backupHash != file.beforeSha256) {
                bool complete = false;
                std::vector<std::wstring> failed;
                Rollback(state, journalDir, complete, failed);
                RemoveCreatedBackups(createdBackups);
                return fail("E_BACKUP_CREATE", L"Резервная копия " + BaseName(file.backupPath) + L" не прошла проверку.");
            }
        }
        WriteJournal(journalDir, state, "backing_up", writeError);
    }
    WriteJournal(journalDir, state, "applying", writeError);

    std::wstring failure;
    std::string failureCode = "E_APPLY_COPY";
    for (std::size_t i = 0; i < state.files.size(); ++i) {
        ResolvedFile& file = state.files[i];
        if (running()) {
            failsRunning(message, report.code);
            failure = message;
            failureCode = report.code;
            break;
        }
        if (options.failCommit && options.failCommit(static_cast<int>(i))) {
            failure = L"Не удалось заменить " + BaseName(file.targetPath) + L".";
            break;
        }
        std::string replaceError;
        const std::wstring staged = stagingDir + L"\\" + Widen(file.source);
        if (!ReplaceWithNew(file.targetPath, staged, file.sha256, file.attributes, replaceError)) {
            failure = L"Не удалось заменить " + BaseName(file.targetPath) + L": " + Widen(replaceError);
            break;
        }
        file.committed = true;
        WriteJournal(journalDir, state, "applying", writeError);
    }
    if (failure.empty()) {
        // Final identity check after all five replacements.
        for (const ResolvedFile& file : state.files) {
            std::string hash;
            if (!FileExists(file.targetPath) || FileSize(file.targetPath) != file.size || !Sha256File(file.targetPath, hash) ||
                hash != file.sha256) {
                failure = L"Файл не прошёл проверку после замены: " + BaseName(file.targetPath) + L".";
                break;
            }
        }
    }
    if (!failure.empty()) {
        phase(InstallPhase::Installing);
        bool complete = false;
        std::vector<std::wstring> failed;
        Rollback(state, journalDir, complete, failed);
        report.journalDir = journalDir;
        if (!complete) {
            WriteJournal(journalDir, state, "rollback_failed", writeError);
            return fail("E_ROLLBACK_INCOMPLETE",
                        L"Откат выполнен не полностью. Не восстановлены: " + JoinErrors(failed) + L". Журнал: " + journalDir);
        }
        RemoveCreatedBackups(createdBackups);
        return fail(failureCode.c_str(), failure + L" Все изменённые файлы возвращены к оригиналу.");
    }

    if (!WriteState(InstallStatePath(stateRoot), state, writeError)) {
        bool complete = false;
        std::vector<std::wstring> failed;
        Rollback(state, journalDir, complete, failed);
        return fail("E_STATE_WRITE", L"Не удалось записать состояние установки: " + Widen(writeError));
    }
    WriteJournal(journalDir, state, "committed", writeError);
    DeleteTree(journalDir);
    DeleteTree(stagingDir);
    RemoveFile(zipPath);
    report.installed = static_cast<int>(state.files.size());
    message = msg::FromUtf8(msg::kDone);
    LogInfo(L"перевод установлен: " + Widen(plan.patchVersion) + L", файлов: " + std::to_wstring(report.installed));
    phase(InstallPhase::Done);
    return true;
}

bool RestoreInstall(const std::wstring& gameDir, const std::wstring& stateRoot, const std::wstring& backupsRoot, const RunningProbe& gameRunning,
                    std::wstring& message, std::string* code) {
    message.clear();
    if (code) code->clear();
    if (gameRunning && gameRunning()) {
        if (code) *code = "E_GAME_RUNNING";
        message = msg::FromUtf8(msg::kGameRunning);
        return false;
    }
    const std::wstring path = InstallStatePath(stateRoot);
    if (!FileExists(path)) {
        if (code) *code = "E_RESTORE_NONE";
        message = msg::FromUtf8(msg::kRestoreNone);
        return true;
    }
    InstallState state;
    std::string status, readError;
    if (!ReadStateFile(path, state, status, readError)) {
        if (code) *code = "E_STATE_READ";
        message = L"Состояние установки повреждено: " + Widen(readError);
        return false;
    }
    if (!IsSamePath(state.gameRoot, gameDir)) {
        if (code) *code = "E_RESTORE_GAME_MISMATCH";
        message = L"Сохранённое состояние относится к другой папке игры: " + state.gameRoot;
        return false;
    }
    std::vector<ResolvedFile> failed;
    std::vector<std::wstring> errors;
    for (ResolvedFile& file : state.files) {
        std::wstring target;
        if (!SafeJoin(gameDir, file.target, target)) {
            failed.push_back(file);
            errors.push_back(L"недопустимый путь в состоянии: " + Widen(file.target));
            if (code) *code = "E_RESTORE_TARGET";
            continue;
        }
        file.targetPath = target;
        file.backupPath = BackupPathFor(target);
        std::string hash;
        if (FileExists(target) && Sha256File(target, hash) && hash == file.beforeSha256) continue;
        if (!FileExists(target) && !DirExists(ParentDir(target))) {
            failed.push_back(file);
            errors.push_back(L"папка файла удалена: " + BaseName(target));
            if (code) *code = "E_RESTORE_TARGET";
            continue;
        }
        std::string backupHash;
        if (!FileExists(file.backupPath) || FileSize(file.backupPath) != file.beforeSize || !Sha256File(file.backupPath, backupHash) ||
            backupHash != file.beforeSha256) {
            failed.push_back(file);
            errors.push_back(L"резервная копия не найдена или повреждена: " + BaseName(target));
            if (code) *code = "E_RESTORE_BACKUP";
            continue;
        }
        std::string copyError;
        if (!CopyFileAtomic(file.backupPath, target, copyError) || !Sha256File(target, hash) || hash != file.beforeSha256) {
            failed.push_back(file);
            errors.push_back(L"не удалось вернуть " + BaseName(target) + L": " + Widen(copyError));
            if (code) *code = "E_RESTORE_TARGET";
            continue;
        }
        if (file.attributes != INVALID_FILE_ATTRIBUTES) SetFileAttributesW(target.c_str(), file.attributes);
    }
    if (!failed.empty()) {
        InstallState partial = state;
        partial.files = failed;
        std::string writeError;
        WriteState(path, partial, writeError);
        if (code) *code = "E_RESTORE_PARTIAL";
        message = msg::FromUtf8(msg::kRestorePartial) + L"\n" + JoinErrors(errors);
        return false;
    }
    RemoveFile(path);
    if (code) code->clear();
    message = msg::FromUtf8(msg::kRestored);
    LogInfo(L"оригинал возвращён, патч: " + Widen(state.patchVersion));
    (void)backupsRoot;
    return true;
}

bool FindUnfinishedJournal(const std::wstring& gameDir, const std::wstring& backupsRoot, std::wstring& journalDir) {
    journalDir.clear();
    int checked = 0;
    for (const std::wstring& dir : JournalDirs(backupsRoot)) {
        if (++checked > 20) break;
        InstallState state;
        std::string status, error;
        if (!ReadStateFile(dir + L"\\journal.json", state, status, error)) continue;
        if (!IsSamePath(state.gameRoot, gameDir)) continue;
        if (status == "backing_up" || status == "applying" || status == "rollback_failed") {
            journalDir = dir;
            return true;
        }
    }
    return false;
}

bool RecoverJournal(const std::wstring& gameDir, const std::wstring& backupsRoot, const RunningProbe& gameRunning, std::wstring& message,
                    std::string* code) {
    message.clear();
    if (code) code->clear();
    if (gameRunning && gameRunning()) {
        if (code) *code = "E_GAME_RUNNING";
        message = msg::FromUtf8(msg::kGameRunning);
        return false;
    }
    std::wstring dir;
    if (!FindUnfinishedJournal(gameDir, backupsRoot, dir)) {
        if (code) *code = "E_RESTORE_NONE";
        message = msg::FromUtf8(msg::kRestoreNone);
        return true;
    }
    InstallState state;
    std::string status, error;
    if (!ReadStateFile(dir + L"\\journal.json", state, status, error)) {
        if (code) *code = "E_STATE_READ";
        message = L"Журнал установки повреждён: " + Widen(error);
        return false;
    }
    bool complete = false;
    std::vector<std::wstring> failed;
    Rollback(state, dir, complete, failed);
    if (!complete) {
        if (code) *code = "E_ROLLBACK_INCOMPLETE";
        message = L"Завершить откат не удалось: " + JoinErrors(failed) + L". Журнал: " + dir;
        return false;
    }
    if (code) code->clear();
    message = msg::FromUtf8(msg::kRolledBack);
    return true;
}

LocalGate EvaluateLocalState(const std::wstring& gameDir, const std::wstring& stateRoot) {
    LocalGate gate;
    if (!IsGameDir(gameDir)) {
        gate.state = LocalState::NoGame;
        gate.code = "E_GAME_INVALID";
        gate.detail = L"Игра не найдена";
        return gate;
    }
    InstallState state;
    std::string status, error;
    if (HasInstallState(stateRoot) && ReadStateFile(InstallStatePath(stateRoot), state, status, error) &&
        IsSamePath(state.gameRoot, gameDir)) {
        gate.state = LocalState::Installed;
        gate.canInstall = true;
        gate.detail = L"перевод " + Widen(state.patchVersion) + L" установлен";
        return gate;
    }
    const RevisionInfo revision = DetectLocalRevision(gameDir);
    if (!revision.valid || revision.revision.empty()) {
        gate.state = LocalState::NeedsGameUpdate;
        gate.code = "E_VERSION_REVISION";
        gate.detail = L"ревизия ресурсов не найдена";
        return gate;
    }
    gate.state = LocalState::Ready;
    gate.canInstall = true;
    return gate;
}

}  // namespace blueward
