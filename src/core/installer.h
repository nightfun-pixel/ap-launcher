#pragma once
// The patch installer: plan, gate, backup, journal, atomic commit, rollback and restore.
// SPEC §5–§7 are normative. Every target gets a sibling `<target>.apbak`; the durable state is
// %LOCALAPPDATA%\BLUEWARD\install.json; the crash journal is under backups\<timestamp>\.

#include <windows.h>

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "core/baseline_state.h"
#include "core/download.h"
#include "core/manifest.h"

namespace blueward {

struct ResolvedFile {
    std::string source, target;
    std::wstring targetPath;  // resolved absolute path (exact or fallback)
    std::wstring exactPath;   // canonical exact path named by the manifest
    std::wstring backupPath;
    std::string resolvedVia;  // exact, fallback-a, fallback-b, missing
    std::string beforeSha256, sha256;
    std::uint64_t beforeSize = 0, size = 0;
    bool missing = false;
    bool backupReused = false;
    DWORD attributes = FILE_ATTRIBUTE_NORMAL;
    bool committed = false;
};

struct InstallPlan {
    std::string patchVersion, gameVersion, resourceRevision, manifestSha256;
    std::uint64_t zipSize = 0;
    std::string zipSha256;
    std::wstring gameDir, resRoot;
    std::vector<ResolvedFile> files;
    std::string errorCode;
};

struct InstallState {
    int schema = 1;
    std::wstring gameRoot;
    std::string patchVersion, gameVersion, resourceRevision, manifestSha256, installedAt;
    std::vector<ResolvedFile> files;
};

enum class InstallPhase { Checking, Downloading, Verifying, Extracting, Installing, Done };
using PhaseFn = std::function<void(InstallPhase)>;
using RunningProbe = std::function<bool()>;

struct InstallOptions {
    RunningProbe gameRunning;
    PhaseFn phase;
    ProgressFn progress;
    std::atomic<bool>* cancel = nullptr;
    // Test hook, null in production: returning true for index `i` simulates a commit failure before
    // that entry is replaced. The normal rollback path then has to restore everything.
    std::function<bool(int)> failCommit;
};

struct InstallReport {
    std::string patchVersion, resourceRevision, code;
    std::wstring journalDir;
    int installed = 0;
    bool alreadyCurrent = false;
};

// Resolves every target: exact path first, then the unique basename fallback under Res root A and
// B. Missing files are carried to Gate B; ambiguity/reparse/search limits fail with a code.
bool BuildPlan(const Manifest& manifest, const std::wstring& gameDir, InstallPlan& plan, std::string& error);

// Gate A: local revision must be known and equal plan.resourceRevision. Unknown skips the gate.
bool CheckGameRevision(const InstallPlan& plan, std::wstring& message, std::string* code = nullptr);
// FR-15: when both are known, patch.game_version must equal the local client version.
bool CheckGameVersion(const InstallPlan& plan, const std::wstring& localVersion, std::wstring& message);
enum class GateResult { Ok, Missing, Mismatch };
// Gate B: size then SHA-256 for every target in manifest order, with the managed re-install rule.
GateResult CheckBefore(const InstallPlan& plan, const std::wstring& stateRoot, std::wstring& detail, std::string* code = nullptr,
                       bool* usedManaged = nullptr);
bool CheckAfter(const InstallPlan& plan);

bool UnpackAndVerify(const InstallPlan& plan, const std::wstring& zipPath, const std::wstring& stagingDir, std::string& error,
                     std::string* code = nullptr);

bool ApplyInstall(const InstallPlan& plan, const std::wstring& zipPath, const std::wstring& stagingDir, const std::wstring& stateRoot,
                  const std::wstring& backupsRoot, const InstallOptions& options, InstallReport& report, std::wstring& message);

bool RestoreInstall(const std::wstring& gameDir, const std::wstring& stateRoot, const std::wstring& backupsRoot, const RunningProbe& gameRunning,
                    std::wstring& message, std::string* code = nullptr);
// Informational success with code E_RESTORE_NONE when nothing is installed.

bool InstalledVersion(const std::wstring& stateRoot, std::string& version, std::wstring* installedAt = nullptr);
bool HasInstallState(const std::wstring& stateRoot);

bool FindUnfinishedJournal(const std::wstring& gameDir, const std::wstring& backupsRoot, std::wstring& journalDir);
bool RecoverJournal(const std::wstring& gameDir, const std::wstring& backupsRoot, const RunningProbe& gameRunning, std::wstring& message,
                    std::string* code = nullptr);

std::wstring InstallStatePath(const std::wstring& stateRoot);
std::wstring JournalDir(const std::wstring& backupsRoot);  // unique backups\<timestamp>

// The offline idle gate: NoGame, Installed (valid install.json for this GR), NeedsGameUpdate (the
// local revision is unknown or malformed), or Ready. Target identities come only from the fetched
// manifest; this gate never carries a compiled path or hash.
LocalGate EvaluateLocalState(const std::wstring& gameDir, const std::wstring& stateRoot);

}  // namespace blueward
