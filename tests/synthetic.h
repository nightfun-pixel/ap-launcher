#pragma once
// A fake AZUPRO_JP tree and a synthetic patch archive. Nothing here touches the installed game.
// The five target paths mirror ops/launcher_targets.json; the bytes are invented.

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

#include "core/download.h"
#include "core/fileio.h"
#include "core/installer.h"
#include "core/json.h"
#include "core/manifest.h"
#include "core/sha256.h"
#include "core/text.h"
#include "zipwrite.h"

namespace testgame {

inline const std::vector<std::string>& Sources() {
    static const std::vector<std::string> names = {"vstring.bytes", "table_data.bytes", "static-text.r", "startup.r", "onest-font.r"};
    return names;
}

// The five 3273853 targets from game-inspect/3273853/targets-3273853.json. Three bundle names moved
// from 3219598; startup.r and onest-font.r still resolve to the builtin files.
inline const std::vector<std::string>& Targets() {
    static const std::vector<std::string> targets = {
        "AzurPromiliaJP_Data/.res/raw_base_package/BundleFiles/heqmts9lcjdvbupu0x2wpg.r",
        "AzurPromiliaJP_Data/.res/raw_streaming_package/BundleFiles/5g1m5b4sbqlfalcxoaquw.r",
        "AzurPromiliaJP_Data/.res/default_package/BundleFiles/h3y7ujukwn9qbhoqhzkkg.r",
        "AzurPromiliaJP_Data/StreamingAssets/.res/static_package/6pq4a7hlp7zidu91dvvww.r",
        "AzurPromiliaJP_Data/StreamingAssets/.res/default_package/hqqoy0ojvzpggukcf5m85w.r",
    };
    return targets;
}

inline std::wstring ToWideSlash(const std::string& relative) {
    std::wstring wide = blueward::Widen(relative);
    for (wchar_t& c : wide)
        if (c == L'/') c = L'\\';
    return wide;
}

inline std::wstring TargetPath(const std::wstring& gameDir, std::size_t index) {
    return gameDir + L"\\" + ToWideSlash(Targets()[index]);
}

inline bool MakeDirs(const std::wstring& path) { return blueward::MakeDirs(path); }

inline std::string BeforeBody(std::size_t index) {
    return "original-" + std::to_string(index) + "-" + std::string(2048 + index * 37, static_cast<char>('a' + index));
}

inline std::string AfterBody(std::size_t index) {
    return "patched-" + std::to_string(index) + "-" + std::string(4096 + index * 53, static_cast<char>('A' + index));
}

struct Synthetic {
    std::wstring root;
    std::wstring gameDir;
    std::wstring zipPath;
    blueward::Manifest manifest;
    std::vector<std::string> before;
    std::vector<std::string> after;
};

inline std::string SerializeManifest(const blueward::Manifest& manifest) {
    blueward::Json files = blueward::Json::Array();
    for (const blueward::PatchFile& file : manifest.patch.files) {
        blueward::Json entry = blueward::Json::Object();
        entry.Set("source", blueward::Json::String(file.source));
        entry.Set("target", blueward::Json::String(file.target));
        entry.Set("before_sha256", blueward::Json::String(file.beforeSha256));
        entry.Set("before_size", blueward::Json::Int(static_cast<std::int64_t>(file.beforeSize)));
        entry.Set("sha256", blueward::Json::String(file.sha256));
        entry.Set("size", blueward::Json::Int(static_cast<std::int64_t>(file.size)));
        files.Push(std::move(entry));
    }
    blueward::Json patch = blueward::Json::Object();
    patch.Set("version", blueward::Json::String(manifest.patch.version));
    patch.Set("game_version", blueward::Json::String(manifest.patch.gameVersion));
    patch.Set("resource_revision", blueward::Json::String(manifest.patch.resourceRevision));
    patch.Set("url", blueward::Json::String(manifest.patch.url));
    patch.Set("sha256", blueward::Json::String(manifest.patch.sha256));
    patch.Set("size", blueward::Json::Int(static_cast<std::int64_t>(manifest.patch.size)));
    patch.Set("files", std::move(files));
    blueward::Json launcher = blueward::Json::Object();
    launcher.Set("version", blueward::Json::String(manifest.launcher.version));
    launcher.Set("url", blueward::Json::String(manifest.launcher.url));
    launcher.Set("sha256", blueward::Json::String(manifest.launcher.sha256));
    launcher.Set("size", blueward::Json::Int(static_cast<std::int64_t>(manifest.launcher.size)));
    blueward::Json document = blueward::Json::Object();
    document.Set("schema", blueward::Json::Int(1));
    document.Set("launcher", std::move(launcher));
    document.Set("patch", std::move(patch));
    return blueward::DumpJson(document);
}

// Builds <root>\AZUPRO_JP with the game marker, the version files and the five original targets,
// plus <root>\patch.zip and a parsed manifest that describes them.
inline bool Build(const std::wstring& root, const std::wstring& revision, Synthetic& out, std::string& error) {
    out = {};
    out.root = root;
    out.gameDir = root + L"\\AZUPRO_JP";
    out.zipPath = root + L"\\patch.zip";
    const std::wstring data = out.gameDir + L"\\AzurPromiliaJP_Data";
    const std::wstring res = data + L"\\StreamingAssets\\.res";
    blueward::MakeDirs(res);
    {
        std::string writeError;
        if (!blueward::WriteFileAtomic(out.gameDir + L"\\AzurPromiliaJP.exe", std::string("MZ fake game"), writeError)) {
            error = writeError;
            return false;
        }
        blueward::WriteFileAtomic(out.gameDir + L"\\game-launcher-config.json", std::string("{\n  \"version\": \"1.0.2\"\n}\n"), writeError);
        blueward::WriteFileAtomic(out.gameDir + L"\\manifest.json", std::string("{\n  \"name\": \"AZUPRO_JP\",\n  \"version\": \"1.0.2\"\n}\n"),
                                  writeError);
    }
    // default/raw_base/raw_streaming carry the requested revision; static_package stays at the
    // built-in 3075915, exactly as a real 3219598 install does.
    const std::pair<const wchar_t*, std::wstring> packages[] = {
        {L"default_package", revision},
        {L"raw_base_package", revision},
        {L"raw_streaming_package", revision},
        {L"static_package", L"3075915"},
    };
    for (const auto& package : packages) {
        const std::wstring dir = res + L"\\" + package.first;
        blueward::MakeDirs(dir);
        const std::wstring stem = std::wstring(L"0release_") + package.first + L"_" + package.second;
        std::string writeError;
        if (!blueward::WriteFileAtomic(dir + L"\\" + stem + L".version", blueward::Narrow(package.second), writeError) ||
            !blueward::WriteFileAtomic(dir + L"\\" + stem + L".hash", std::string(32, '0'), writeError)) {
            error = writeError;
            return false;
        }
    }

    out.manifest.schema = 1;
    out.manifest.launcher.version = "0.1.0";
    out.manifest.launcher.url = blueward::kLauncherExeUrl;
    out.manifest.launcher.size = 6;
    out.manifest.launcher.sha256 = "0000000000000000000000000000000000000000000000000000000000000000";
    out.manifest.patch.version = "2026.11.27-1";
    out.manifest.patch.gameVersion = "1.0.2";
    out.manifest.patch.resourceRevision = blueward::Narrow(revision);
    out.manifest.patch.url = "https://github.com/nightfun-pixel/ap-launcher/releases/download/patch-" +
                             blueward::Narrow(revision) + "/patch-" + blueward::Narrow(revision) + ".zip";
    out.manifest.patch.size = 0;
    out.manifest.patch.sha256 = "0000000000000000000000000000000000000000000000000000000000000000";

    std::vector<testzip::Item> items;
    for (std::size_t i = 0; i < Sources().size(); ++i) {
        out.before.push_back(BeforeBody(i));
        out.after.push_back(AfterBody(i));
        const std::wstring target = TargetPath(out.gameDir, i);
        blueward::MakeDirs(blueward::ParentDir(target));
        std::string writeError;
        if (!blueward::WriteFileAtomic(target, out.before[i], writeError)) {
            error = writeError;
            return false;
        }
        std::string hash, beforeHash;
        blueward::Sha256String(out.after[i], hash);
        blueward::Sha256String(out.before[i], beforeHash);
        blueward::PatchFile file;
        file.source = Sources()[i];
        file.target = Targets()[i];
        file.beforeSha256 = beforeHash;
        file.beforeSize = out.before[i].size();
        file.sha256 = hash;
        file.size = out.after[i].size();
        out.manifest.patch.files.push_back(file);
        testzip::Item item;
        item.name = Sources()[i];
        item.data = out.after[i];
        item.deflate = i % 2 == 1;
        items.push_back(std::move(item));
    }

    if (!testzip::Write(out.zipPath, items, error)) return false;
    out.manifest.patch.size = blueward::FileSize(out.zipPath);
    blueward::Sha256File(out.zipPath, out.manifest.patch.sha256);

    const std::string manifestText = SerializeManifest(out.manifest);
    {
        std::string writeError;
        if (!blueward::WriteFileAtomic(root + L"\\manifest.json", manifestText, writeError)) {
            error = writeError;
            return false;
        }
    }
    if (!testzip::Write(out.zipPath, items, error)) return false;
    out.manifest.patch.size = blueward::FileSize(out.zipPath);
    blueward::Sha256File(out.zipPath, out.manifest.patch.sha256);

    blueward::Manifest parsed;
    std::string parseError;
    if (!blueward::ParseManifest(manifestText, parsed, parseError)) {
        error = parseError;
        return false;
    }
    out.manifest = parsed;
    return true;
}

inline std::wstring BuiltinVersionPath(const std::wstring& gameDir, const std::wstring& package, const std::wstring& revision) {
    return gameDir + L"\\AzurPromiliaJP_Data\\StreamingAssets\\.res\\" + package + L"\\0release_" + package + L"_" + revision +
           L".version";
}

inline std::wstring DeltaManifestPath(const std::wstring& gameDir, const std::wstring& package, const std::wstring& revision) {
    return gameDir + L"\\AzurPromiliaJP_Data\\.res\\" + package + L"\\ManifestFiles\\0release_" + package + L"_" + revision + L".bytes";
}

// Rewrites one target with arbitrary bytes (to test the before gate).
inline bool Damage(const std::wstring& gameDir, std::size_t index, const std::string& bytes) {
    std::string error;
    return blueward::WriteFileAtomic(TargetPath(gameDir, index), bytes, error);
}

inline bool RestoreOriginal(const std::wstring& gameDir, std::size_t index) {
    std::string error;
    return blueward::WriteFileAtomic(TargetPath(gameDir, index), BeforeBody(index), error);
}

inline std::wstring ResRootOf(const std::wstring& gameDir) {
    return gameDir + L"\\AzurPromiliaJP_Data\\StreamingAssets\\.res";
}

}  // namespace testgame
