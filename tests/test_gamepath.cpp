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
        CHECK(blueward::LocalResourceRevision(patch.gameDir).empty());
        CHECK(blueward::RemoveFile(rootA + L"\\0release_default_package_1111111.version"));
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
}
