#pragma once
// Azur Promilia JP locations: the game folder, the .res root, the local resource revision and the
// two game processes the launcher must not run over.

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace blueward {

inline constexpr const wchar_t* kGameExe = L"AzurPromiliaJP.exe";
inline constexpr const wchar_t* kGameLauncherExe = L"AZUPRO_JP_Gamelauncher.exe";
inline constexpr const wchar_t* kDataDir = L"AzurPromiliaJP_Data";
inline constexpr const wchar_t* kResDir = L"StreamingAssets\\.res";
inline constexpr const wchar_t* kDeltaResDir = L".res";

// Requires AzurPromiliaJP.exe (a file) and AzurPromiliaJP_Data\StreamingAssets\.res (a folder).
// The delta folder AzurPromiliaJP_Data\.res is optional.
bool IsGameDir(const std::wstring& dir);

// Absolute, backslashes, no trailing separator, no \\?\ prefix. Accepts the game folder, its
// parent when the child AZUPRO_JP is the game, or AzurPromiliaJP.exe itself. Empty when unusable.
std::wstring NormalizeGameDir(const std::wstring& chosen);

// The .res root: <game>\AzurPromiliaJP_Data\StreamingAssets\.res
std::wstring ResRoot(const std::wstring& gameDir);
// The game's own version from <game>\manifest.json (`version`), empty when unreadable.
std::wstring ReadGameVersion(const std::wstring& gameDir);

enum class RevisionSource { None, Builtin, Delta };

// The highest local resource revision and where it came from. Builtin revisions are read from
// StreamingAssets\.res\<pkg>\0release_<pkg>_<rev>.version; delta revisions from
// .res\<pkg>\ManifestFiles\0release_<pkg>_<rev>.bytes. A malformed candidate makes the whole
// result invalid rather than silently falling back to an older revision.
struct RevisionInfo {
    std::wstring revision;
    RevisionSource source = RevisionSource::None;
    std::vector<std::wstring> packages;
    bool valid = false;
};

RevisionInfo DetectLocalRevision(const std::wstring& gameDir);

// Package revisions parsed from <res>\<package>\0release_<package>_<revision>.version.
// Key: package name, value: revision digits.
bool ReadResVersions(const std::wstring& resRoot, std::map<std::wstring, std::wstring>& versions);
// DetectLocalRevision(...).revision, or empty when unknown/malformed.
std::wstring LocalResourceRevision(const std::wstring& gameDir);

// A case-insensitive search of the process list by executable name.
bool IsProcessRunning(const std::wstring& exeName);
bool GameProcessesRunning(const std::function<bool(const std::wstring&)>& probe);

// Places worth probing on this machine: <exeDir>\.., every drive's YostarGames/AZUPRO_JP,
// Program Files\YostarGames. The caller appends the usual AZUPRO_JP child.
std::vector<std::wstring> CommonGameRoots(const std::wstring& exeDir);
std::wstring AutoDetectGameDir(const std::vector<std::wstring>& roots);

}  // namespace blueward
