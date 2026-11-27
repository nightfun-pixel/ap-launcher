#pragma once
// The update feed: one manifest that describes the current patch archive and the current launcher.
// Schema 1 is produced by the Studio publisher; this file validates it strictly before any byte of
// the game is touched. SPEC §3 is normative.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace blueward {

// The only GitHub URLs the launcher knows. The repository owner appears in these two URLs and
// nowhere else in the project; see README.md.
inline constexpr const char* kFeedUrl =
    "https://github.com/nightfun-pixel/ap-launcher/releases/latest/download/manifest.json";
inline constexpr const char* kLauncherExeUrl =
    "https://github.com/nightfun-pixel/ap-launcher/releases/latest/download/BLUEWARD.exe";

inline constexpr std::uint64_t kManifestLimit = 262144;
inline constexpr std::uint64_t kLauncherSizeLimit = 67108864;
inline constexpr std::uint64_t kArchiveSizeLimit = 2147483648ull;
inline constexpr std::uint64_t kPayloadSizeLimit = 1073741824ull;

struct LauncherInfo {
    std::string version;
    std::string url;
    std::string sha256;
    std::uint64_t size = 0;
};

struct PatchFile {
    std::string source;  // root-level name inside the zip
    std::string target;  // forward-slash path relative to the game root
    std::string beforeSha256;
    std::uint64_t beforeSize = 0;
    std::string sha256;
    std::uint64_t size = 0;
};

struct PatchInfo {
    std::string version;
    std::string gameVersion;
    std::string resourceRevision;
    std::string url;
    std::string sha256;
    std::uint64_t size = 0;
    std::vector<PatchFile> files;
};

struct Manifest {
    int schema = 0;
    LauncherInfo launcher;
    PatchInfo patch;
    std::string sha256;  // SHA-256 of the exact bytes parsed (FR-24)
};

// The five payloads a release may carry, in publisher order.
const std::vector<std::string>& PayloadNames();
bool IsPayloadName(std::string_view name);

// Schema 1, exactly five payloads, safe targets under AzurPromiliaJP_Data/, canonical URLs,
// strict sizes, lower-case hashes, no duplicate keys, depth at most 32, valid UTF-8.
// On failure `code` (when given) receives the stable E_MANIFEST_* / E_NET_* code.
bool ParseManifest(std::string_view json, Manifest& out, std::string& error, std::string* code = nullptr);

// A target must be relative, forward-slash, under AzurPromiliaJP_Data/, at most 240 characters,
// with at least two segments and no escape, reserved name, trailing dot or space.
bool IsAllowedPatchTarget(std::string_view target, std::string& why);

// https + github.com + exact canonical path for the manifest, launcher or patch archive.
// A loopback address is accepted for the tests only.
bool IsAllowedDownloadUrl(std::string_view url, std::string& why);
// Redirects go only to github.com / objects.githubusercontent.com (or loopback in tests), HTTPS,
// port 443, no userinfo, no fragment. A signed query is allowed on a redirect target.
bool IsAllowedRedirectUrl(std::string_view url, std::string& why);
bool IsLoopbackUrl(const std::string& url, std::string& why);
std::string UrlHost(std::string_view url);

bool IsCanonicalLauncherUrl(std::string_view url);
bool IsCanonicalManifestUrl(std::string_view url);
bool IsCanonicalPatchUrl(std::string_view url, std::string_view revision);

std::string ResolveUrl(std::string_view base, std::string_view relative);
// The per-revision feed beside the latest manifest: .../releases/latest/download/manifest-<rev>.json.
std::string ManifestUrlForRevision(std::string_view revision);
bool IsCanonicalManifestUrlForRevision(std::string_view url, std::string_view revision);
int CompareVersions(std::string_view a, std::string_view b);  // 1.0.10 > 1.0.9

// True when the value is numeric x.y.z (each part digits, no leading zero unless "0").
bool IsNumericVersion(std::string_view value);

}  // namespace blueward
