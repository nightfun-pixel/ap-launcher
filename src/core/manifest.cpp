#include "core/manifest.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <set>

#include "core/json.h"
#include "core/sha256.h"
#include "core/text.h"

namespace blueward {

namespace {

const char* kDataPrefix = "AzurPromiliaJP_Data/";
const char* kReleasePrefix = "https://github.com/nightfun-pixel/ap-launcher/releases/";

bool IsDigits(std::string_view value, std::size_t maxLength) {
    if (value.empty() || value.size() > maxLength) return false;
    for (const char c : value)
        if (c < '0' || c > '9') return false;
    return true;
}

std::string LowerAscii(std::string value) {
    for (char& c : value)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return value;
}

struct UrlParts {
    std::string scheme, host, port, path, query;
    bool hasQuery = false, hasFragment = false, credentials = false, ipLiteral = false, trailingDot = false;
    bool valid = false;
};

UrlParts SplitUrl(std::string_view url) {
    UrlParts parts;
    const std::size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string_view::npos) return parts;
    parts.scheme = LowerAscii(std::string(url.substr(0, schemeEnd)));
    if (parts.scheme != "http" && parts.scheme != "https") return parts;
    std::string_view rest = url.substr(schemeEnd + 3);
    const std::size_t authorityEnd = rest.find_first_of("/?#");
    const std::string_view authority = rest.substr(0, authorityEnd);
    const std::string_view tail = authorityEnd == std::string_view::npos ? std::string_view() : rest.substr(authorityEnd);
    if (authority.find('@') != std::string_view::npos) {
        parts.credentials = true;
        return parts;
    }
    if (authority.empty()) return parts;
    std::string_view host = authority;
    const std::size_t colon = authority.rfind(':');
    if (authority.front() == '[') {
        const std::size_t close = authority.find(']');
        if (close == std::string_view::npos) return parts;
        host = authority.substr(0, close + 1);
        parts.ipLiteral = true;
        if (close + 1 < authority.size() && authority[close + 1] == ':') parts.port = std::string(authority.substr(close + 2));
    } else if (colon != std::string_view::npos && authority.find(':') == colon) {
        host = authority.substr(0, colon);
        parts.port = std::string(authority.substr(colon + 1));
    }
    parts.host = LowerAscii(std::string(host));
    if (parts.host.empty()) return parts;
    if (parts.host.back() == '.') {
        parts.trailingDot = true;
        parts.host.pop_back();
    }
    if (!parts.ipLiteral) {
        bool onlyDigitsAndDots = true;
        for (const char c : parts.host)
            if (!((c >= '0' && c <= '9') || c == '.')) onlyDigitsAndDots = false;
        if (onlyDigitsAndDots && parts.host.find('.') != std::string::npos) parts.ipLiteral = true;
    }
    if (!parts.port.empty() && !IsDigits(parts.port, 5)) return parts;
    std::size_t pathEnd = tail.find_first_of("?#");
    parts.path = std::string(tail.substr(0, pathEnd));
    if (parts.path.empty()) parts.path = "/";
    if (pathEnd != std::string_view::npos && tail[pathEnd] == '?') {
        parts.hasQuery = true;
        const std::size_t hash = tail.find('#', pathEnd);
        parts.query = std::string(tail.substr(pathEnd + 1, hash == std::string_view::npos ? std::string_view::npos : hash - pathEnd - 1));
        pathEnd = hash;
    }
    if (pathEnd != std::string_view::npos && tail[pathEnd] == '#') parts.hasFragment = true;
    parts.valid = true;
    return parts;
}

bool IsLoopbackHost(const std::string& host) {
    return host == "localhost" || host == "127.0.0.1" || host == "[::1]" || host == "::1";
}

bool StandardHttpsPort(const UrlParts& parts) { return parts.port.empty() || parts.port == "443"; }

bool CanonicalGithubHttps(const UrlParts& parts) {
    return parts.valid && parts.scheme == "https" && parts.host == "github.com" && StandardHttpsPort(parts) && !parts.credentials &&
           !parts.trailingDot && !parts.ipLiteral && !parts.hasQuery && !parts.hasFragment;
}

bool Hex64(const Json* value, std::string& out) {
    if (!value || !value->isString()) return false;
    const std::string& text = value->asString();
    if (!IsSha256Hex(text)) return false;
    out = text;
    return true;
}

bool PositiveSize(const Json* value, std::uint64_t minBytes, std::uint64_t maxBytes, std::uint64_t& out) {
    if (!value || !value->isInteger()) return false;
    const std::int64_t size = value->asInt();
    if (size < static_cast<std::int64_t>(minBytes) || static_cast<std::uint64_t>(size) > maxBytes) return false;
    out = static_cast<std::uint64_t>(size);
    return true;
}

bool ExactKeys(const Json& object, std::initializer_list<const char*> keys) {
    if (!object.isObject() || object.members().size() != keys.size()) return false;
    for (const char* key : keys)
        if (!object.Find(key)) return false;
    return true;
}

bool HasDuplicateKeys(const Json& value) {
    if (value.isObject()) {
        std::set<std::string> keys;
        for (const auto& member : value.members()) {
            if (!keys.insert(member.first).second) return true;
            if (HasDuplicateKeys(member.second)) return true;
        }
    } else if (value.isArray()) {
        for (const Json& item : value.items())
            if (HasDuplicateKeys(item)) return true;
    }
    return false;
}

int DepthOf(const Json& value) {
    if (value.isObject()) {
        int depth = 0;
        for (const auto& member : value.members()) depth = std::max(depth, DepthOf(member.second));
        return depth + 1;
    }
    if (value.isArray()) {
        int depth = 0;
        for (const Json& item : value.items()) depth = std::max(depth, DepthOf(item));
        return depth + 1;
    }
    return 1;
}

bool ValidUtf8(std::string_view text) {
    std::size_t i = 0;
    while (i < text.size()) {
        const unsigned char b = static_cast<unsigned char>(text[i]);
        if (b < 0x80) {
            ++i;
            continue;
        }
        int extra = 0;
        unsigned cp = 0;
        if (b >= 0xC2 && b <= 0xDF) {
            extra = 1;
            cp = b & 0x1F;
        } else if (b >= 0xE0 && b <= 0xEF) {
            extra = 2;
            cp = b & 0x0F;
        } else if (b >= 0xF0 && b <= 0xF4) {
            extra = 3;
            cp = b & 0x07;
        } else {
            return false;
        }
        if (i + extra >= text.size()) return false;
        for (int k = 0; k < extra; ++k) {
            const unsigned char c = static_cast<unsigned char>(text[i + 1 + k]);
            if ((c & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (c & 0x3F);
        }
        if (extra == 2 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))) return false;
        if (extra == 3 && (cp < 0x10000 || cp > 0x10FFFF)) return false;
        i += static_cast<std::size_t>(extra) + 1;
    }
    return true;
}

bool ReservedName(std::string name) {
    name = LowerAscii(std::move(name));
    const std::size_t dot = name.find('.');
    if (dot != std::string::npos) name = name.substr(0, dot);
    if (name == "con" || name == "prn" || name == "aux" || name == "nul") return true;
    if (name.size() == 3 && (name.rfind("com", 0) == 0 || name.rfind("lpt", 0) == 0)) return name[2] >= '1' && name[2] <= '9';
    return false;
}

bool NumericVersion(const std::string& value) {
    if (value.empty() || value.size() > 32) return false;
    int parts = 0;
    std::size_t at = 0;
    while (at <= value.size()) {
        const std::size_t dot = value.find('.', at);
        const std::string part = value.substr(at, dot == std::string::npos ? std::string::npos : dot - at);
        if (part.empty() || part.size() > 9) return false;
        for (const char c : part)
            if (c < '0' || c > '9') return false;
        if (part.size() > 1 && part.front() == '0') return false;
        ++parts;
        if (dot == std::string::npos) break;
        at = dot + 1;
    }
    return parts == 3;
}

bool PatchVersion(const std::string& value) {
    if (value.empty() || value.size() > 64) return false;
    const unsigned char first = static_cast<unsigned char>(value.front());
    if (!(std::isalnum(first) != 0)) return false;
    for (const char c : value) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (!(std::isalnum(u) != 0 || c == '.' || c == '_' || c == '+' || c == '-')) return false;
    }
    return true;
}

std::string FieldError(const char* field, const char* why) { return std::string(field) + ": " + why; }

bool TargetRules(std::string_view target, std::string& why, bool checkReserved) {
    if (target.empty()) {
        why = "target is empty";
        return false;
    }
    if (target.size() > 240) {
        why = "target is longer than 240 characters";
        return false;
    }
    for (const char c : target) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == 0x7F) {
            why = "target contains a control character";
            return false;
        }
    }
    if (target.find('\\') != std::string_view::npos) {
        why = "target must use forward slashes";
        return false;
    }
    if (target.find(':') != std::string_view::npos) {
        why = "target must not contain a drive letter or colon";
        return false;
    }
    if (target.find('%') != std::string_view::npos) {
        why = "target must not contain percent escapes";
        return false;
    }
    if (target.front() == '/' || target.front() == '~' || target.front() == '$') {
        why = "target must be relative";
        return false;
    }
    if (target.rfind(kDataPrefix, 0) != 0) {
        why = std::string("target must stay under ") + kDataPrefix;
        return false;
    }
    int segments = 0;
    std::size_t at = 0;
    while (at <= target.size()) {
        const std::size_t next = target.find('/', at);
        const std::string_view part = target.substr(at, next == std::string_view::npos ? std::string_view::npos : next - at);
        if (part.empty() || part == "." || part == "..") {
            why = "target must not contain empty, '.' or '..' segments";
            return false;
        }
        if (part.back() == '.' || part.back() == ' ') {
            why = "target segment must not end with a dot or space";
            return false;
        }
        if (checkReserved && ReservedName(std::string(part))) {
            why = "target uses a reserved Windows name";
            return false;
        }
        ++segments;
        if (next == std::string_view::npos) break;
        at = next + 1;
    }
    if (segments < 2) {
        why = "target must have at least two segments";
        return false;
    }
    return true;
}

}  // namespace

const std::vector<std::string>& PayloadNames() {
    static const std::vector<std::string> names = {"vstring.bytes", "table_data.bytes", "static-text.r", "startup.r", "onest-font.r"};
    return names;
}

bool IsPayloadName(std::string_view name) {
    for (const std::string& known : PayloadNames())
        if (name == known) return true;
    return false;
}

bool IsNumericVersion(std::string_view value) { return NumericVersion(std::string(value)); }

bool IsAllowedPatchTarget(std::string_view target, std::string& why) { return TargetRules(target, why, true); }

std::string UrlHost(std::string_view url) { return SplitUrl(url).host; }

bool IsLoopbackUrl(const std::string& url, std::string& why) {
    const UrlParts parts = SplitUrl(url);
    if (!parts.valid) {
        why = "not a valid http or https address";
        return false;
    }
    if (!IsLoopbackHost(parts.host)) {
        why = "not a loopback address";
        return false;
    }
    return true;
}

bool IsCanonicalManifestUrl(std::string_view url) {
    return url == kFeedUrl;
}

bool IsCanonicalLauncherUrl(std::string_view url) {
    return url == kLauncherExeUrl;
}

bool IsCanonicalPatchUrl(std::string_view url, std::string_view revision) {
    if (!IsDigits(revision, 12)) return false;
    const std::string expected = std::string(kReleasePrefix) + "download/patch-" + std::string(revision) + "/patch-" + std::string(revision) + ".zip";
    return url == expected;
}

std::string ManifestUrlForRevision(std::string_view revision) {
    if (!IsDigits(revision, 12)) return {};
    std::string base = kFeedUrl;
    const std::string suffix = "/manifest.json";
    if (base.size() <= suffix.size() || base.compare(base.size() - suffix.size(), suffix.size(), suffix) != 0) return {};
    base.resize(base.size() - suffix.size());
    return base + "/manifest-" + std::string(revision) + ".json";
}

bool IsCanonicalManifestUrlForRevision(std::string_view url, std::string_view revision) {
    const std::string expected = ManifestUrlForRevision(revision);
    return !expected.empty() && url == expected;
}

bool IsAllowedDownloadUrl(std::string_view url, std::string& why) {
    const UrlParts parts = SplitUrl(url);
    if (!parts.valid) {
        why = "not a valid http or https address";
        return false;
    }
    if (IsLoopbackHost(parts.host)) return true;  // test-only loopback exception
    if (!CanonicalGithubHttps(parts)) {
        why = "only the canonical github.com https release URLs are allowed";
        return false;
    }
    if (IsCanonicalManifestUrl(url) || IsCanonicalLauncherUrl(url)) return true;
    // The patch path is derived from the canonical launcher URL.
    const std::string canonical = kLauncherExeUrl;
    const std::string repo = canonical.substr(0, canonical.find("/releases/"));
    const std::string host = "https://github.com";
    if (repo.rfind(host, 0) != 0) {
        why = "the canonical launcher URL is malformed";
        return false;
    }
    const std::string path = parts.path;
    const std::string manifestPrefix = repo.substr(host.size()) + "/releases/latest/download/manifest-";
    if (path.rfind(manifestPrefix, 0) == 0 && path.size() > manifestPrefix.size() + 5 &&
        path.compare(path.size() - 5, 5, ".json") == 0) {
        const std::string revision = path.substr(manifestPrefix.size(), path.size() - manifestPrefix.size() - 5);
        if (IsCanonicalManifestUrlForRevision(url, revision)) return true;
    }
    const std::string pathPrefix = repo.substr(host.size()) + "/releases/download/patch-";
    if (path.rfind(pathPrefix, 0) == 0) {
        const std::size_t slash = path.find('/', pathPrefix.size());
        if (slash == std::string::npos) {
            why = "the patch URL is not the tagged archive";
            return false;
        }
        const std::string revision = path.substr(pathPrefix.size(), slash - pathPrefix.size());
        if (IsCanonicalPatchUrl(url, revision)) return true;
    }
    why = "only the canonical github.com https release URLs are allowed";
    return false;
}

bool IsAllowedRedirectUrl(std::string_view url, std::string& why) {
    const UrlParts parts = SplitUrl(url);
    if (!parts.valid) {
        why = "redirect is not a valid address";
        return false;
    }
    if (IsLoopbackHost(parts.host)) return true;  // test-only loopback exception
    if (parts.scheme != "https") {
        why = "a redirect must stay on HTTPS";
        return false;
    }
    if (parts.port.empty() ? false : parts.port != "443") {
        why = "a redirect must use port 443";
        return false;
    }
    if (parts.credentials || parts.trailingDot || parts.ipLiteral || parts.hasFragment) {
        why = "a redirect with userinfo, an IP literal, a trailing dot or a fragment is refused";
        return false;
    }
    // Exact hosts only. GitHub redirects release assets to release-assets.githubusercontent.com;
    // a suffixed lookalike such as release-assets.githubusercontent.com.evil is a different host
    // and is refused below.
    if (parts.host == "github.com" || parts.host == "objects.githubusercontent.com" ||
        parts.host == "release-assets.githubusercontent.com")
        return true;
    why = "redirect host " + parts.host + " is not allowed";
    return false;
}

std::string ResolveUrl(std::string_view base, std::string_view relative) {
    if (relative.rfind("https://", 0) == 0 || relative.rfind("http://", 0) == 0) return std::string(relative);
    const std::size_t schemeEnd = base.find("://");
    if (schemeEnd == std::string_view::npos) return std::string(relative);
    const std::size_t authorityEnd = base.find('/', schemeEnd + 3);
    const std::string origin = std::string(base.substr(0, authorityEnd == std::string_view::npos ? base.size() : authorityEnd));
    if (!relative.empty() && relative.front() == '/') return origin + std::string(relative);
    const std::size_t lastSlash = base.find_last_of('/');
    if (lastSlash == std::string_view::npos || lastSlash < schemeEnd + 3) return origin + "/" + std::string(relative);
    return std::string(base.substr(0, lastSlash + 1)) + std::string(relative);
}

int CompareVersions(std::string_view a, std::string_view b) {
    auto numeric = [](std::string_view part, long long& out) {
        out = 0;
        if (part.empty()) return true;
        if (part.size() > 9) return false;
        for (const char c : part) {
            if (c < '0' || c > '9') return false;
            out = out * 10 + (c - '0');
        }
        return true;
    };
    std::size_t ai = 0, bi = 0;
    for (int index = 0; index < 8; ++index) {
        std::string_view ap, bp;
        if (ai <= a.size()) {
            const std::size_t dot = a.find('.', ai);
            ap = a.substr(ai, dot == std::string_view::npos ? std::string_view::npos : dot - ai);
            ai = dot == std::string_view::npos ? a.size() + 1 : dot + 1;
        }
        if (bi <= b.size()) {
            const std::size_t dot = b.find('.', bi);
            bp = b.substr(bi, dot == std::string_view::npos ? std::string_view::npos : dot - bi);
            bi = dot == std::string_view::npos ? b.size() + 1 : dot + 1;
        }
        if (ap.empty() && bp.empty()) return 0;
        long long an = 0, bn = 0;
        const bool aNumeric = numeric(ap, an), bNumeric = numeric(bp, bn);
        if (aNumeric && bNumeric) {
            if (an != bn) return an < bn ? -1 : 1;
        } else {
            const int cmp = ap.compare(bp);
            if (cmp != 0) return cmp < 0 ? -1 : 1;
        }
    }
    return 0;
}

bool ParseManifest(std::string_view json, Manifest& out, std::string& error, std::string* code) {
    out = {};
    error.clear();
    if (code) code->clear();
    auto fail = [&](const char* errorCode, std::string message) {
        if (code) *code = errorCode;
        error = std::move(message);
        return false;
    };
    std::string_view text = json;
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF)
        text.remove_prefix(3);
    if (!ValidUtf8(text)) return fail("E_MANIFEST_PARSE", "manifest is not valid UTF-8");
    Json document;
    std::string parseError;
    if (!ParseJson(text, document, parseError)) return fail("E_MANIFEST_PARSE", "manifest is not valid JSON: " + parseError);
    if (!document.isObject()) return fail("E_MANIFEST_SCHEMA", "manifest must be an object");
    if (DepthOf(document) > 32) return fail("E_MANIFEST_SCHEMA", "manifest is nested deeper than 32 levels");
    if (HasDuplicateKeys(document)) return fail("E_MANIFEST_SCHEMA", "manifest contains a duplicate object key");
    if (!ExactKeys(document, {"schema", "launcher", "patch"})) return fail("E_MANIFEST_SCHEMA", "manifest must have exactly schema, launcher, patch");

    const Json* schema = document.Find("schema");
    if (!schema || !schema->isInteger() || schema->asInt() != 1) return fail("E_MANIFEST_SCHEMA", "schema: must be 1");
    const Json* launcher = document.Find("launcher");
    if (!launcher || !ExactKeys(*launcher, {"version", "url", "sha256", "size"}))
        return fail("E_MANIFEST_SCHEMA", "launcher: must have exactly version, url, sha256, size");
    const Json* patch = document.Find("patch");
    if (!patch || !ExactKeys(*patch, {"version", "game_version", "resource_revision", "url", "sha256", "size", "files"}))
        return fail("E_MANIFEST_SCHEMA", "patch: must have exactly version, game_version, resource_revision, url, sha256, size, files");

    Manifest manifest;
    manifest.schema = 1;
    manifest.sha256 = [&] {
        std::string hash;
        Sha256String(json, hash);
        return hash;
    }();

    const Json* launcherVersion = launcher->Find("version");
    if (!launcherVersion || !launcherVersion->isString() || !NumericVersion(launcherVersion->asString()))
        return fail("E_MANIFEST_SCHEMA", FieldError("launcher.version", "must be numeric x.y.z, 32 characters or fewer"));
    manifest.launcher.version = launcherVersion->asString();
    const Json* launcherUrl = launcher->Find("url");
    if (!launcherUrl || !launcherUrl->isString() || !IsCanonicalLauncherUrl(launcherUrl->asString()))
        return fail("E_NET_URL", FieldError("launcher.url", "must be the canonical latest BLUEWARD.exe URL"));
    manifest.launcher.url = launcherUrl->asString();
    if (!Hex64(launcher->Find("sha256"), manifest.launcher.sha256))
        return fail("E_MANIFEST_SCHEMA", FieldError("launcher.sha256", "must be 64 lower-case hex characters"));
    if (!PositiveSize(launcher->Find("size"), 1, kLauncherSizeLimit, manifest.launcher.size))
        return fail("E_MANIFEST_SCHEMA", FieldError("launcher.size", "must be 1..67108864"));

    const Json* patchVersion = patch->Find("version");
    if (!patchVersion || !patchVersion->isString() || !PatchVersion(patchVersion->asString()))
        return fail("E_MANIFEST_SCHEMA", FieldError("patch.version", "must match the patch version grammar"));
    manifest.patch.version = patchVersion->asString();
    const Json* gameVersion = patch->Find("game_version");
    if (!gameVersion || !gameVersion->isString() || !NumericVersion(gameVersion->asString()))
        return fail("E_MANIFEST_SCHEMA", FieldError("patch.game_version", "must be numeric x.y.z"));
    manifest.patch.gameVersion = gameVersion->asString();
    const Json* revision = patch->Find("resource_revision");
    if (!revision || !revision->isString() || !IsDigits(revision->asString(), 12))
        return fail("E_MANIFEST_SCHEMA", FieldError("patch.resource_revision", "must be a string of 1..12 digits"));
    manifest.patch.resourceRevision = revision->asString();
    const Json* patchUrl = patch->Find("url");
    if (!patchUrl || !patchUrl->isString() || !IsCanonicalPatchUrl(patchUrl->asString(), manifest.patch.resourceRevision))
        return fail("E_NET_URL", FieldError("patch.url", "must be the canonical tagged patch archive URL"));
    manifest.patch.url = patchUrl->asString();
    if (!Hex64(patch->Find("sha256"), manifest.patch.sha256))
        return fail("E_MANIFEST_SCHEMA", FieldError("patch.sha256", "must be 64 lower-case hex characters"));
    if (!PositiveSize(patch->Find("size"), 1, kArchiveSizeLimit, manifest.patch.size))
        return fail("E_MANIFEST_SCHEMA", FieldError("patch.size", "must be 1..2147483648"));

    const Json* files = patch->Find("files");
    if (!files || !files->isArray() || files->items().size() != PayloadNames().size())
        return fail("E_MANIFEST_SCHEMA", FieldError("patch.files", "must list exactly the five payloads"));
    bool seen[5] = {false, false, false, false, false};
    std::vector<std::string> targets;
    std::vector<std::string> basenames;
    std::uint64_t totalSize = 0;
    for (const Json& item : files->items()) {
        if (!ExactKeys(item, {"source", "target", "before_sha256", "before_size", "sha256", "size"}))
            return fail("E_MANIFEST_SCHEMA", "patch.files[]: has the wrong keys");
        const Json* source = item.Find("source");
        if (!source || !source->isString() || !IsPayloadName(source->asString()))
            return fail("E_MANIFEST_SOURCE", FieldError("patch.files[].source", "is not one of the five payloads"));
        std::size_t nameIndex = 0;
        for (std::size_t i = 0; i < PayloadNames().size(); ++i)
            if (PayloadNames()[i] == source->asString()) nameIndex = i;
        if (seen[nameIndex]) return fail("E_MANIFEST_SOURCE", FieldError("patch.files[].source", "is listed twice"));
        seen[nameIndex] = true;
        const Json* target = item.Find("target");
        if (!target || !target->isString()) return fail("E_MANIFEST_TARGET", FieldError("patch.files[].target", "must be a string"));
        std::string why;
        if (!IsAllowedPatchTarget(target->asString(), why)) return fail("E_MANIFEST_TARGET", FieldError("patch.files[].target", why.c_str()));
        if (std::find(targets.begin(), targets.end(), target->asString()) != targets.end())
            return fail("E_MANIFEST_TARGET", FieldError("patch.files[].target", "is listed twice"));
        targets.push_back(target->asString());
        const std::string basename = target->asString().substr(target->asString().find_last_of('/') + 1);
        const std::string lowerBasename = LowerAscii(basename);
        if (std::find(basenames.begin(), basenames.end(), lowerBasename) != basenames.end())
            return fail("E_MANIFEST_TARGET", FieldError("patch.files[].target", "has a duplicate basename"));
        basenames.push_back(lowerBasename);

        PatchFile file;
        file.source = source->asString();
        file.target = target->asString();
        if (!Hex64(item.Find("before_sha256"), file.beforeSha256))
            return fail("E_MANIFEST_SCHEMA", FieldError("patch.files[].before_sha256", "must be 64 lower-case hex characters"));
        if (!PositiveSize(item.Find("before_size"), 1, kPayloadSizeLimit, file.beforeSize))
            return fail("E_MANIFEST_SCHEMA", FieldError("patch.files[].before_size", "must be 1..1073741824"));
        if (!Hex64(item.Find("sha256"), file.sha256))
            return fail("E_MANIFEST_SCHEMA", FieldError("patch.files[].sha256", "must be 64 lower-case hex characters"));
        if (!PositiveSize(item.Find("size"), 1, kPayloadSizeLimit, file.size))
            return fail("E_MANIFEST_SCHEMA", FieldError("patch.files[].size", "must be 1..1073741824"));
        totalSize += file.size;
        if (totalSize > kArchiveSizeLimit) return fail("E_MANIFEST_SCHEMA", "the five payloads exceed 2147483648 bytes");
        manifest.patch.files.push_back(std::move(file));
    }
    out = std::move(manifest);
    return true;
}

}  // namespace blueward
