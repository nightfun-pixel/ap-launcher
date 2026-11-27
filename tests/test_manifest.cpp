#include "check.h"

#include <string>

#include "core/manifest.h"
#include "core/sha256.h"

namespace {

const char* kValid = R"({
  "schema": 1,
  "launcher": {
    "version": "0.1.0",
    "url": "https://github.com/nightfun-pixel/ap-launcher/releases/latest/download/BLUEWARD.exe",
    "sha256": "1111111111111111111111111111111111111111111111111111111111111111",
    "size": 12345678
  },
  "patch": {
    "version": "2026.11.27-1",
    "game_version": "1.0.2",
    "resource_revision": "3273853",
    "url": "https://github.com/nightfun-pixel/ap-launcher/releases/download/patch-3273853/patch-3273853.zip",
    "sha256": "2222222222222222222222222222222222222222222222222222222222222222",
    "size": 241479593,
    "files": [
      {"source":"vstring.bytes","target":"AzurPromiliaJP_Data/.res/raw_base_package/BundleFiles/heqmts9lcjdvbupu0x2wpg.r","before_sha256":"60e09fa31ac7e43e2a74cb7e722cf19b3331a188817125a086a57a17a34987ee","before_size":134055486,"sha256":"4444444444444444444444444444444444444444444444444444444444444444","size":143827740},
      {"source":"table_data.bytes","target":"AzurPromiliaJP_Data/.res/raw_streaming_package/BundleFiles/5g1m5b4sbqlfalcxoaquw.r","before_sha256":"4369839fd46cd2c821f33dc2b92c46cc87f4413a215be0abe17461ec743896c2","before_size":28840130,"sha256":"6666666666666666666666666666666666666666666666666666666666666666","size":28840130},
      {"source":"static-text.r","target":"AzurPromiliaJP_Data/.res/default_package/BundleFiles/h3y7ujukwn9qbhoqhzkkg.r","before_sha256":"9771462bacb96c7ee544fe53d0d2a4e56e3576b1336d2a2ec6653bfef1a6a138","before_size":4993822,"sha256":"8888888888888888888888888888888888888888888888888888888888888888","size":4993892},
      {"source":"startup.r","target":"AzurPromiliaJP_Data/StreamingAssets/.res/static_package/6pq4a7hlp7zidu91dvvww.r","before_sha256":"c7c80b98b263f161583a2ab52a5b7039636acbabcac8c365f9f37224d11bc4b3","before_size":58707878,"sha256":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","size":58707878},
      {"source":"onest-font.r","target":"AzurPromiliaJP_Data/StreamingAssets/.res/default_package/hqqoy0ojvzpggukcf5m85w.r","before_sha256":"a584d52f66f66a32dc2a2e9bfd7a119eed10290fc79795b6730896a5440d2a7f","before_size":5109385,"sha256":"cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc","size":5109385}
    ]
  }
})";

bool Parses(const std::string& text, std::string* code = nullptr) {
    blueward::Manifest manifest;
    std::string error;
    return blueward::ParseManifest(text, manifest, error, code);
}

std::string ReplaceFirst(const std::string& text, const std::string& needle, const std::string& value) {
    const std::size_t at = text.find(needle);
    if (at == std::string::npos) return {};
    std::string out = text;
    out.replace(at, needle.size(), value);
    return out;
}

}  // namespace

void RunManifestTests() {
    {
        // T-035: golden manifest, five entries, both identities preserved.
        blueward::Manifest manifest;
        std::string error;
        std::string code;
        CHECK(blueward::ParseManifest(kValid, manifest, error, &code));
        CHECK(error.empty());
        CHECK(code.empty());
        CHECK(manifest.schema == 1);
        CHECK(manifest.launcher.version == "0.1.0");
        CHECK(manifest.patch.resourceRevision == "3273853");
        CHECK(manifest.patch.files.size() == 5);
        if (manifest.patch.files.size() == 5) {
            CHECK(manifest.patch.files[0].source == "vstring.bytes");
            CHECK(manifest.patch.files[0].target == "AzurPromiliaJP_Data/.res/raw_base_package/BundleFiles/heqmts9lcjdvbupu0x2wpg.r");
            CHECK(manifest.patch.files[0].beforeSize == 134055486);
            CHECK(manifest.patch.files[0].size == 143827740);
            CHECK(manifest.patch.files[4].source == "onest-font.r");
        }
        std::string hash;
        blueward::Sha256String(kValid, hash);
        CHECK(manifest.sha256 == hash);
    }
    {
        // T-037: target grammar, reserved names, trailing dot/space, duplicate basename.
        const char* badTargets[] = {"AzurPromiliaJP_Data/../evil.bytes",
                                    "/abs.bytes",
                                    "C:/AzurPromiliaJP_Data/x",
                                    "AzurPromiliaJP_Data\\x",
                                    "AzurPromiliaJP_Data/%2e%2e/x",
                                    "Other/x",
                                    "AzurPromiliaJP_Data/a/../../b",
                                    "AzurPromiliaJP_Data/.res/CON",
                                    "AzurPromiliaJP_Data/.res/name.",
                                    "AzurPromiliaJP_Data/.res/name ",
                                    "AzurPromiliaJP_Data//x"};
        for (const char* target : badTargets) {
            std::string why;
            CHECK_MSG(!blueward::IsAllowedPatchTarget(target, why), target);
            CHECK(!why.empty());
        }
        std::string why;
        CHECK(blueward::IsAllowedPatchTarget("AzurPromiliaJP_Data/.res/default_package/x.r", why));
        std::string tooLong = "AzurPromiliaJP_Data/.res/";
        tooLong += std::string(241 - tooLong.size(), 'a');
        CHECK(!blueward::IsAllowedPatchTarget(tooLong, why));

        const std::string duplicateBasename = ReplaceFirst(
            std::string(kValid), "AzurPromiliaJP_Data/.res/raw_streaming_package/BundleFiles/5g1m5b4sbqlfalcxoaquw.r",
            "AzurPromiliaJP_Data/.res/other_package/BundleFiles/heqmts9lcjdvbupu0x2wpg.r");
        std::string code;
        CHECK(!Parses(duplicateBasename, &code));
        CHECK(code == "E_MANIFEST_TARGET");
    }
    {
        // T-038: sources.
        std::string code;
        CHECK(!Parses(ReplaceFirst(kValid, "\"vstring.bytes\"", "\"VSTRING.BYTES\""), &code));
        CHECK(code == "E_MANIFEST_SOURCE");
        code.clear();
        CHECK(!Parses(ReplaceFirst(kValid, "\"onest-font.r\"", "\"evil.bin\""), &code));
        CHECK(code == "E_MANIFEST_SOURCE");
    }
    {
        // T-039: hashes and sizes.
        std::string code;
        CHECK(!Parses(ReplaceFirst(kValid, "60e09fa31ac7e43e2a74cb7e722cf19b3331a188817125a086a57a17a34987ee",
                                   "60e09fa31ac7e43e2a74cb7e722cf19b3331a188817125a086a57a17a34987e"),
                      &code));
        CHECK(code == "E_MANIFEST_SCHEMA");
        code.clear();
        CHECK(!Parses(ReplaceFirst(kValid, "60e09fa31ac7e43e2a74cb7e722cf19b3331a188817125a086a57a17a34987ee",
                                   "60e09fa31ac7e43e2a74cb7e722cf19b3331a188817125a086a57a17a34987eZ"),
                      &code));
        CHECK(code == "E_MANIFEST_SCHEMA");
        code.clear();
        CHECK(!Parses(ReplaceFirst(kValid, "\"before_size\":134055486", "\"before_size\":0"), &code));
        CHECK(code == "E_MANIFEST_SCHEMA");
        code.clear();
        CHECK(!Parses(ReplaceFirst(kValid, "\"size\":143827740", "\"size\":1073741825"), &code));
        CHECK(code == "E_MANIFEST_SCHEMA");
    }
    {
        // T-040: launcher block.
        std::string code;
        CHECK(!Parses(ReplaceFirst(kValid, "\"version\": \"0.1.0\"", "\"version\": \"1.0\""), &code));
        CHECK(code == "E_MANIFEST_SCHEMA");
        code.clear();
        CHECK(!Parses(ReplaceFirst(kValid, "\"version\": \"0.1.0\"", "\"version\": \"01.0.0\""), &code));
        CHECK(code == "E_MANIFEST_SCHEMA");
        code.clear();
        CHECK(!Parses(ReplaceFirst(kValid,
                                   "https://github.com/nightfun-pixel/ap-launcher/releases/latest/download/BLUEWARD.exe",
                                   "http://github.com/nightfun-pixel/ap-launcher/releases/latest/download/BLUEWARD.exe"),
                      &code));
        CHECK(code == "E_NET_URL");
        code.clear();
        CHECK(!Parses(ReplaceFirst(kValid, "\"size\": 12345678", "\"size\": 67108865"), &code));
        CHECK(code == "E_MANIFEST_SCHEMA");
    }
    {
        // T-041, T-042: the revision is data, not a compiled constant.
        std::string code;
        blueward::Manifest parsed;
        std::string error;
        CHECK(blueward::ParseManifest(kValid, parsed, error, &code));
        CHECK(parsed.patch.resourceRevision == "3273853");
        CHECK(parsed.patch.files.size() == 5);
        code.clear();
        CHECK(Parses(ReplaceFirst(
            ReplaceFirst(kValid, "\"resource_revision\": \"3273853\"", "\"resource_revision\": \"3219598\""),
            "patch-3273853/patch-3273853.zip", "patch-3219598/patch-3219598.zip")));
        code.clear();
        CHECK(Parses(ReplaceFirst(
            ReplaceFirst(kValid, "\"resource_revision\": \"3273853\"", "\"resource_revision\": \"3075915\""),
            "patch-3273853/patch-3273853.zip", "patch-3075915/patch-3075915.zip")));
        code.clear();
        CHECK(!Parses(ReplaceFirst(kValid, "\"resource_revision\": \"3273853\"", "\"resource_revision\": 3273853"), &code));
        CHECK(code == "E_MANIFEST_SCHEMA");
        code.clear();
        CHECK(!Parses(ReplaceFirst(kValid, "\"resource_revision\": \"3273853\"", "\"resource_revision\": \"\""), &code));
        CHECK(code == "E_MANIFEST_SCHEMA");
        code.clear();
        CHECK(!Parses(ReplaceFirst(kValid, "\"resource_revision\": \"3273853\"", "\"resource_revision\": \"12a\""), &code));
        CHECK(code == "E_MANIFEST_SCHEMA");
        code.clear();
        CHECK(!Parses(ReplaceFirst(kValid, "\"resource_revision\": \"3273853\"", "\"resource_revision\": \"1234567890123\""), &code));
        CHECK(code == "E_MANIFEST_SCHEMA");
        code.clear();
        CHECK(!Parses(ReplaceFirst(kValid, "patch-3273853/patch-3273853.zip", "patch-3273853/patch-3273854.zip"), &code));
        CHECK(code == "E_NET_URL");
        code.clear();
        CHECK(!Parses(ReplaceFirst(kValid, "\"game_version\": \"1.0.2\"", "\"game_version\": \"1.0\""), &code));
        CHECK(code == "E_MANIFEST_SCHEMA");
    }
    {
        // The per-revision feed URL is canonical and derived only from the latest manifest URL.
        const std::string fallback = blueward::ManifestUrlForRevision("3273853");
        CHECK(fallback == "https://github.com/nightfun-pixel/ap-launcher/releases/latest/download/manifest-3273853.json");
        std::string why;
        CHECK(blueward::IsAllowedDownloadUrl(fallback, why));
        CHECK(!blueward::IsAllowedDownloadUrl(fallback + "?x=1", why));
        CHECK(!blueward::IsAllowedDownloadUrl(blueward::ManifestUrlForRevision("abc"), why));
        CHECK(!blueward::IsAllowedDownloadUrl(blueward::ManifestUrlForRevision("1234567890123"), why));
    }
    {
        // T-036: exact keys, duplicate keys, depth, BOM, invalid UTF-8.
        std::string code;
        CHECK(!Parses(ReplaceFirst(kValid, "\"schema\": 1,", "\"schema\": 1, \"extra\": true,"), &code));
        CHECK(code == "E_MANIFEST_SCHEMA");
        code.clear();
        CHECK(!Parses(ReplaceFirst(kValid, "\"schema\": 1,", "\"schema\": 1, \"schema\": 1,"), &code));
        CHECK(code == "E_MANIFEST_SCHEMA");
        std::string deep;
        for (int i = 0; i < 33; ++i) deep += "[";
        for (int i = 0; i < 33; ++i) deep += "]";
        code.clear();
        CHECK(!Parses(deep, &code));
        CHECK(code == "E_MANIFEST_SCHEMA");
        std::string bom = "\xEF\xBB\xBF";
        bom += kValid;
        CHECK(Parses(bom));
        std::string invalid = kValid;
        invalid.insert(invalid.find("\"0.1.0\""), "\xFF");
        code.clear();
        CHECK(!Parses(invalid, &code));
        CHECK(code == "E_MANIFEST_PARSE");
    }
    {
        // T-045: only canonical initial URLs.
        std::string why;
        CHECK(blueward::IsAllowedDownloadUrl(blueward::kFeedUrl, why));
        CHECK(blueward::IsAllowedDownloadUrl(blueward::kLauncherExeUrl, why));
        CHECK(blueward::IsAllowedDownloadUrl(
            "https://github.com/nightfun-pixel/ap-launcher/releases/download/patch-3219598/patch-3219598.zip", why));
        CHECK(!blueward::IsAllowedDownloadUrl("https://github.com/x", why));
        CHECK(!blueward::IsAllowedDownloadUrl("https://github.com/nightfun-pixel/ap-launcher/releases/latest/download/manifest.json?x=1", why));
        CHECK(!blueward::IsAllowedDownloadUrl("https://objects.githubusercontent.com/x", why));
        CHECK(!blueward::IsAllowedDownloadUrl("http://github.com/nightfun-pixel/ap-launcher/releases/latest/download/manifest.json", why));
        CHECK(blueward::IsAllowedRedirectUrl("https://objects.githubusercontent.com/x?sig=1", why));
        CHECK(blueward::IsAllowedRedirectUrl(
            "https://release-assets.githubusercontent.com/github-production-release-asset/1474923047/e2f925e0-42fa-40c2-a416-3e379038d18e?sp=r&sv=2018-11-09&sr=b&sig=QSghGuGbDvVnBqIZJjn%2Boyg6WxSluHLf56ofBaX5ZJo%3D",
            why));
        CHECK(blueward::IsAllowedRedirectUrl("https://github.com/nightfun-pixel/ap-launcher/releases/download/patch-3219598/patch-3219598.zip", why));
        CHECK(!blueward::IsAllowedRedirectUrl("https://release-assets.githubusercontent.com.evil.example/x", why));
        CHECK(!blueward::IsAllowedRedirectUrl("https://evil.example/x", why));
        CHECK(!blueward::IsAllowedRedirectUrl("http://release-assets.githubusercontent.com/x", why));
        CHECK(!blueward::IsAllowedRedirectUrl("https://user:pass@release-assets.githubusercontent.com/x", why));
        CHECK(!blueward::IsAllowedRedirectUrl("https://release-assets.githubusercontent.com:8443/x", why));
        CHECK(!blueward::IsAllowedRedirectUrl("https://evil.example/release-assets.githubusercontent.com", why));
        CHECK(!blueward::IsAllowedRedirectUrl("http://github.com/x", why));
        CHECK(!blueward::IsAllowedRedirectUrl("https://user:pass@github.com/x", why));
    }
    {
        CHECK(blueward::CompareVersions("1.0.10", "1.0.9") > 0);
        CHECK(blueward::CompareVersions("1.0.9", "1.0.10") < 0);
        CHECK(blueward::CompareVersions("0.1.0", "0.1.0") == 0);
        CHECK(blueward::CompareVersions("1.2", "1.2.0") == 0);
        CHECK(blueward::IsNumericVersion("1.0.2"));
        CHECK(!blueward::IsNumericVersion("1.0"));
        CHECK(!blueward::IsNumericVersion("01.0.0"));
    }
    {
        CHECK(blueward::ResolveUrl("https://github.com/a/b/manifest.json", "patch.zip") == "https://github.com/a/b/patch.zip");
        CHECK(blueward::ResolveUrl("https://github.com/a/b/manifest.json", "/x/patch.zip") == "https://github.com/x/patch.zip");
        CHECK(blueward::ResolveUrl("https://github.com/a/b/manifest.json", "https://github.com/c/d.zip") == "https://github.com/c/d.zip");
    }
}
