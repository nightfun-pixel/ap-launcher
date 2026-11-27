#include "check.h"
#include "helpers.h"
#include "synthetic.h"

#include <string>

#include "core/fileio.h"
#include "core/gamepath.h"

void RunGamePathTests() {
    testgame::Synthetic patch;
    std::string error;
    const std::wstring root = testutil::Path(L"gamepath");
    CHECK(testgame::Build(root, L"3219598", patch, error));
    {
        // T-026: all three markers are required.
        CHECK(blueward::IsGameDir(patch.gameDir));
        CHECK(!blueward::IsGameDir(root));
        CHECK(blueward::ReadGameVersion(patch.gameDir) == L"1.0.2");
    }
    {
        // T-025: GR, parent, and the Data folder normalise; grandparent is not searched.
        CHECK(blueward::NormalizeGameDir(patch.gameDir) == patch.gameDir);
        CHECK(blueward::NormalizeGameDir(patch.gameDir + L"\\") == patch.gameDir);
        CHECK(blueward::NormalizeGameDir(root) == patch.gameDir);
        CHECK(blueward::NormalizeGameDir(patch.gameDir + L"\\AzurPromiliaJP_Data") == patch.gameDir);
        CHECK(blueward::NormalizeGameDir(patch.gameDir + L"\\AzurPromiliaJP.exe") == patch.gameDir);
        CHECK(blueward::NormalizeGameDir(testutil::Path(L"nope")).empty());
        CHECK(blueward::NormalizeGameDir(blueward::ParentDir(root)).empty());
    }
    {
        // T-027: UNC, device and relative paths are refused.
        CHECK(blueward::NormalizeGameDir(L"\\\\server\\share\\AZUPRO_JP").empty());
        CHECK(blueward::NormalizeGameDir(L"\\\\?\\" + patch.gameDir).empty());
        CHECK(blueward::NormalizeGameDir(L"\\\\.\\" + patch.gameDir).empty());
        CHECK(blueward::NormalizeGameDir(L"relative\\AZUPRO_JP").empty());
    }
    {
        // T-026: a broken tree is rejected.
        const std::wstring broken = testutil::Path(L"broken-game");
        blueward::MakeDirs(broken);
        CHECK(testutil::Write(broken + L"\\AzurPromiliaJP.exe", "MZ"));
        CHECK(!blueward::IsGameDir(broken));
        CHECK(blueward::NormalizeGameDir(broken).empty());
    }
    {
        // T-030: B is primary, A cross-checks, static is never used.
        std::map<std::wstring, std::wstring> versions;
        CHECK(blueward::ReadResVersions(testgame::ResRootOf(patch.gameDir), versions));
        CHECK(versions.size() >= 3);
        CHECK(versions[L"default_package"] == L"3219598");
        CHECK(versions[L"static_package"] == L"3075915");
        CHECK(blueward::LocalResourceRevision(patch.gameDir) == L"3219598");

        const std::wstring rootA = patch.gameDir + L"\\AzurPromiliaJP_Data\\.res\\default_package";
        blueward::MakeDirs(rootA);
        CHECK(testutil::Write(rootA + L"\\0release_default_package_1111111.version", "1111111"));
        // Root A version files are not the delta: only ManifestFiles\*.bytes count there.
        CHECK(blueward::LocalResourceRevision(patch.gameDir) == L"3219598");
        CHECK(blueward::RemoveFile(rootA + L"\\0release_default_package_1111111.version"));

        // A second builtin version for one package is malformed, not an older revision to use.
        const std::wstring second =
            testgame::BuiltinVersionPath(patch.gameDir, L"default_package", L"2222222");
        CHECK(testutil::Write(second, "2222222"));
        CHECK(blueward::LocalResourceRevision(patch.gameDir).empty());
        CHECK(blueward::RemoveFile(second));
        CHECK(blueward::LocalResourceRevision(patch.gameDir) == L"3219598");
    }
    {
        // T-029: client version falls back to the first 64 KiB of manifest.json.
        const std::wstring fallback = testutil::Path(L"version-fallback");
        CHECK(testgame::Build(fallback, L"3219598", patch, error));
        CHECK(blueward::RemoveFile(patch.gameDir + L"\\game-launcher-config.json"));
        CHECK(blueward::ReadGameVersion(patch.gameDir) == L"1.0.2");
        CHECK(blueward::RemoveFile(patch.gameDir + L"\\manifest.json"));
        CHECK(blueward::ReadGameVersion(patch.gameDir).empty());
    }
    {
        // T-024: bounded auto-detection, first valid root wins.
        const std::wstring detected = root + L"\\AZUPRO_JP";
        const std::vector<std::wstring> roots = {testutil::Path(L"nothing-here"), root};
        CHECK(blueward::AutoDetectGameDir(roots) == detected);
        const std::vector<std::wstring> none = {testutil::Path(L"nothing-here")};
        CHECK(blueward::AutoDetectGameDir(none).empty());
        CHECK(!blueward::CommonGameRoots(testutil::ExeDir()).empty());
    }
    {
        // T-022/T-090/T-091: process probes.
        CHECK(blueward::GameProcessesRunning([](const std::wstring& name) { return name == blueward::kGameExe; }));
        CHECK(blueward::GameProcessesRunning([](const std::wstring& name) { return name == blueward::kGameLauncherExe; }));
        CHECK(!blueward::GameProcessesRunning([](const std::wstring&) { return false; }));
        CHECK(blueward::IsProcessRunning(L"blueward_tests.exe"));
        CHECK(!blueward::IsProcessRunning(L"does-not-exist-blueward.exe"));
    }
    {
        const std::wstring game = testutil::Path(L"mismatch-game");
        testgame::Synthetic old;
        CHECK(testgame::Build(game, L"3075915", old, error));
        CHECK(blueward::LocalResourceRevision(old.gameDir) == L"3075915");
    }
    {
        // Builtin only: the version files carry the revision, the source is builtin.
        const std::wstring game = testutil::Path(L"revision-builtin");
        testgame::Synthetic tree;
        CHECK(testgame::Build(game, L"3075915", tree, error));
        const blueward::RevisionInfo info = blueward::DetectLocalRevision(tree.gameDir);
        CHECK(info.valid);
        CHECK(info.revision == L"3075915");
        CHECK(info.source == blueward::RevisionSource::Builtin);
        CHECK(info.packages.size() >= 3);
    }
    {
        // Both roots: the 3273853 delta outranks the 3075915 builtin files.
        const std::wstring game = testutil::Path(L"revision-both");
        testgame::Synthetic tree;
        CHECK(testgame::Build(game, L"3075915", tree, error));
        for (const wchar_t* package : {L"default_package", L"raw_base_package", L"raw_streaming_package"}) {
            const std::wstring path = testgame::DeltaManifestPath(tree.gameDir, package, L"3273853");
            CHECK(testutil::Write(path, std::string(32, 'x')));
        }
        const blueward::RevisionInfo info = blueward::DetectLocalRevision(tree.gameDir);
        CHECK(info.valid);
        CHECK(info.revision == L"3273853");
        CHECK(info.source == blueward::RevisionSource::Delta);
        CHECK(blueward::LocalResourceRevision(tree.gameDir) == L"3273853");
        bool hasDefault = false;
        for (const std::wstring& package : info.packages) hasDefault = hasDefault || package == L"default_package";
        CHECK(hasDefault);
    }
    {
        // Delta only: no .version file anywhere, the ManifestFiles bytes still identify the revision.
        const std::wstring game = testutil::Path(L"revision-delta");
        testgame::Synthetic tree;
        CHECK(testgame::Build(game, L"3075915", tree, error));
        for (const wchar_t* package : {L"default_package", L"raw_base_package", L"raw_streaming_package", L"static_package"}) {
            const std::wstring revision = std::wstring(package) == L"static_package" ? L"3075915" : L"3075915";
            CHECK(blueward::RemoveFile(testgame::BuiltinVersionPath(tree.gameDir, package, revision)));
            CHECK(testutil::Write(testgame::DeltaManifestPath(tree.gameDir, package, L"3273853"), std::string(16, 'y')));
        }
        const blueward::RevisionInfo info = blueward::DetectLocalRevision(tree.gameDir);
        CHECK(info.valid);
        CHECK(info.revision == L"3273853");
        CHECK(info.source == blueward::RevisionSource::Delta);
        CHECK(blueward::LocalResourceRevision(tree.gameDir) == L"3273853");
    }
    {
        // Absent and malformed are invalid, not silently zero.
        const std::wstring absentGame = testutil::Path(L"revision-absent");
        testgame::Synthetic absent;
        CHECK(testgame::Build(absentGame, L"3075915", absent, error));
        for (const wchar_t* package : {L"default_package", L"raw_base_package", L"raw_streaming_package", L"static_package"})
            CHECK(blueward::RemoveFile(testgame::BuiltinVersionPath(absent.gameDir, package, L"3075915")));
        const blueward::RevisionInfo none = blueward::DetectLocalRevision(absent.gameDir);
        CHECK(!none.valid);
        CHECK(none.revision.empty());
        CHECK(none.source == blueward::RevisionSource::None);
        CHECK(blueward::LocalResourceRevision(absent.gameDir).empty());

        const std::wstring badGame = testutil::Path(L"revision-malformed");
        testgame::Synthetic bad;
        CHECK(testgame::Build(badGame, L"3075915", bad, error));
        CHECK(testutil::Write(testgame::BuiltinVersionPath(bad.gameDir, L"default_package", L"3075915"), "9999999"));
        const blueward::RevisionInfo malformed = blueward::DetectLocalRevision(bad.gameDir);
        CHECK(!malformed.valid);
        CHECK(blueward::LocalResourceRevision(bad.gameDir).empty());
    }
    {
        // The delta folder is optional; the game root stays valid without AzurPromiliaJP_Data\.res.
        const std::wstring game = testutil::Path(L"revision-no-delta-root");
        testgame::Synthetic tree;
        CHECK(testgame::Build(game, L"3075915", tree, error));
        CHECK(blueward::DeleteTree(tree.gameDir + L"\\AzurPromiliaJP_Data\\.res"));
        CHECK(!blueward::DirExists(tree.gameDir + L"\\AzurPromiliaJP_Data\\.res"));
        CHECK(blueward::IsGameDir(tree.gameDir));
        CHECK(blueward::DetectLocalRevision(tree.gameDir).revision == L"3075915");
    }
}
