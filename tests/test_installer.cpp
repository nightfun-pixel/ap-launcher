#include "check.h"
#include "helpers.h"
#include "synthetic.h"

#include <string>

#include "core/fileio.h"
#include "core/installer.h"
#include "core/messages.h"

namespace {

struct Fixture {
    testgame::Synthetic patch;
    blueward::InstallPlan plan;
    std::wstring root, state, backups, staging;
    std::string error;

    bool Build(const std::wstring& name, const std::wstring& revision = L"3219598") {
        root = testutil::Path(name);
        if (!testgame::Build(root, revision, patch, error)) return false;
        state = root + L"\\state";
        backups = root + L"\\backups";
        staging = root + L"\\staging";
        blueward::MakeDirs(state);
        blueward::MakeDirs(backups);
        return blueward::BuildPlan(patch.manifest, patch.gameDir, plan, error);
    }
};

blueward::InstallOptions Options(bool running = false) {
    blueward::InstallOptions options;
    options.gameRunning = [running] { return running; };
    return options;
}

std::wstring VersionMessage() { return blueward::msg::FromUtf8(blueward::msg::kVersion); }

}  // namespace

void RunInstallerTests() {
    {
        // T-065, T-094, T-096, T-102: clean install, then the same install is a no-op.
        Fixture fixture;
        CHECK_MSG(fixture.Build(L"install-ok"), fixture.error);
        CHECK(fixture.plan.files.size() == 5);
        CHECK(fixture.plan.resRoot == testgame::ResRootOf(fixture.patch.gameDir));
        std::wstring message;
        CHECK(blueward::CheckGameRevision(fixture.plan, message));
        std::string code;
        bool managed = false;
        CHECK(blueward::CheckBefore(fixture.plan, fixture.state, message, &code, &managed) == blueward::GateResult::Ok);
        CHECK(!managed);
        CHECK(!blueward::CheckAfter(fixture.plan));

        blueward::InstallReport report;
        CHECK_MSG(blueward::ApplyInstall(fixture.plan, fixture.patch.zipPath, fixture.staging, fixture.state, fixture.backups,
                                         Options(), report, message),
                  blueward::Narrow(message));
        CHECK(report.installed == 5);
        CHECK(report.code.empty());
        CHECK(blueward::HasInstallState(fixture.state));
        {
            std::wstring journal;
            CHECK(!blueward::FindUnfinishedJournal(fixture.patch.gameDir, fixture.backups, journal));
        }
        for (std::size_t i = 0; i < 5; ++i) {
            const std::wstring target = testgame::TargetPath(fixture.patch.gameDir, i);
            CHECK(testutil::Read(target) == fixture.patch.after[i]);
            CHECK(testutil::Read(target + L".apbak") == fixture.patch.before[i]);
            CHECK(testutil::HashFile(target) == fixture.patch.manifest.patch.files[i].sha256);
        }
        CHECK(blueward::CheckAfter(fixture.plan));
        // The managed rule allows a follow-up check against the installed tree.
        message.clear();
        managed = false;
        CHECK(blueward::CheckBefore(fixture.plan, fixture.state, message, &code, &managed) == blueward::GateResult::Ok);
        CHECK(managed);

        blueward::InstallReport again;
        message.clear();
        CHECK(blueward::ApplyInstall(fixture.plan, fixture.patch.zipPath, fixture.staging, fixture.state, fixture.backups, Options(),
                                     again, message));
        CHECK(again.alreadyCurrent);
        CHECK(message == blueward::msg::FromUtf8(blueward::msg::kAlreadyInstalled));

        // T-108, T-115: restore keeps every .apbak (FR-58).
        message.clear();
        CHECK_MSG(blueward::RestoreInstall(fixture.patch.gameDir, fixture.state, fixture.backups, [] { return false; }, message, &code),
                  blueward::Narrow(message));
        CHECK(code.empty());
        for (std::size_t i = 0; i < 5; ++i) {
            const std::wstring target = testgame::TargetPath(fixture.patch.gameDir, i);
            CHECK(testutil::Read(target) == fixture.patch.before[i]);
            CHECK(testutil::Exists(target + L".apbak"));
        }
        CHECK(!blueward::HasInstallState(fixture.state));
        // T-109: a second restore is an informational no-op.
        message.clear();
        code.clear();
        CHECK(blueward::RestoreInstall(fixture.patch.gameDir, fixture.state, fixture.backups, [] { return false; }, message, &code));
        CHECK(code == "E_RESTORE_NONE");
        CHECK(message == blueward::msg::FromUtf8(blueward::msg::kRestoreNone));
    }
    {
        // T-066: a missing target produces the exact primary message and detail.
        Fixture fixture;
        CHECK(fixture.Build(L"install-gate-missing"));
        CHECK(blueward::RemoveFile(testgame::TargetPath(fixture.patch.gameDir, 2)));
        std::wstring message;
        std::string code;
        CHECK(blueward::CheckBefore(fixture.plan, fixture.state, message, &code) == blueward::GateResult::Missing);
        const std::wstring basename = blueward::BaseName(testgame::TargetPath(fixture.patch.gameDir, 2));
        CHECK_MSG(message == blueward::msg::GateDetailMissing(basename), blueward::Narrow(message));
        blueward::InstallReport report;
        CHECK(!blueward::ApplyInstall(fixture.plan, fixture.patch.zipPath, fixture.staging, fixture.state, fixture.backups, Options(), report,
                                      message));
        CHECK(report.code == "E_VERSION_BEFORE");
        CHECK_MSG(message == VersionMessage() + L" " + blueward::msg::GateDetailMissing(basename), blueward::Narrow(message));
        CHECK(!blueward::HasInstallState(fixture.state));
    }
    {
        // T-067, T-068: hash and size mismatches, size checked before hashing.
        Fixture fixture;
        CHECK(fixture.Build(L"install-gate-mismatch"));
        CHECK(testgame::Damage(fixture.patch.gameDir, 1, std::string(testgame::BeforeBody(1).size(), 'z')));
        std::wstring message;
        std::string code;
        CHECK(blueward::CheckBefore(fixture.plan, fixture.state, message, &code) == blueward::GateResult::Mismatch);
        const std::wstring basename = blueward::BaseName(testgame::TargetPath(fixture.patch.gameDir, 1));
        CHECK_MSG(message == blueward::msg::GateDetailHash(basename), blueward::Narrow(message));

        Fixture sizeFixture;
        CHECK(sizeFixture.Build(L"install-gate-size"));
        const std::string original = testgame::BeforeBody(2);
        CHECK(testgame::Damage(sizeFixture.patch.gameDir, 2, original.substr(0, original.size() - 1)));
        message.clear();
        CHECK(blueward::CheckBefore(sizeFixture.plan, sizeFixture.state, message, &code) == blueward::GateResult::Mismatch);
        const std::wstring sizeBasename = blueward::BaseName(testgame::TargetPath(sizeFixture.patch.gameDir, 2));
        CHECK_MSG(message == blueward::msg::GateDetailSize(sizeBasename, sizeFixture.plan.files[2].beforeSize, original.size() - 1),
                  blueward::Narrow(message));
    }
    {
        // T-064, T-069: Gate A at 3075915 uses M_VERSION with no detail.
        Fixture fixture;
        CHECK(fixture.Build(L"install-version-mismatch", L"3075915"));
        fixture.patch.manifest.patch.resourceRevision = "3219598";
        CHECK(blueward::BuildPlan(fixture.patch.manifest, fixture.patch.gameDir, fixture.plan, fixture.error));
        std::wstring message;
        std::string code;
        CHECK(!blueward::CheckGameRevision(fixture.plan, message, &code));
        CHECK(code == "E_VERSION_REVISION");
        CHECK(message == VersionMessage());
        blueward::InstallReport report;
        CHECK(!blueward::ApplyInstall(fixture.plan, fixture.patch.zipPath, fixture.staging, fixture.state, fixture.backups, Options(),
                                      report, message));
        CHECK(report.code == "E_VERSION_REVISION");
        CHECK(report.installed == 0);
        CHECK(!blueward::HasInstallState(fixture.state));
    }
    {
        // T-090, T-091: a running game refuses before any write.
        Fixture fixture;
        CHECK(fixture.Build(L"install-running"));
        blueward::InstallReport report;
        std::wstring message;
        CHECK(!blueward::ApplyInstall(fixture.plan, fixture.patch.zipPath, fixture.staging, fixture.state, fixture.backups, Options(true),
                                      report, message));
        CHECK_MSG(report.code == "E_GAME_RUNNING", report.code);
        CHECK_MSG(message == blueward::msg::FromUtf8(blueward::msg::kGameRunning), blueward::Narrow(message));
        for (std::size_t i = 0; i < 5; ++i) CHECK(testutil::Read(testgame::TargetPath(fixture.patch.gameDir, i)) == fixture.patch.before[i]);
        CHECK(!blueward::HasInstallState(fixture.state));
    }
    {
        // T-099: a mid-install failure rolls every committed target back.
        Fixture fixture;
        CHECK(fixture.Build(L"install-rollback"));
        blueward::InstallOptions options = Options();
        options.failCommit = [](int index) { return index == 2; };
        blueward::InstallReport report;
        std::wstring message;
        CHECK(!blueward::ApplyInstall(fixture.plan, fixture.patch.zipPath, fixture.staging, fixture.state, fixture.backups, options, report,
                                      message));
        CHECK(!message.empty());
        for (std::size_t i = 0; i < 5; ++i) {
            const std::wstring target = testgame::TargetPath(fixture.patch.gameDir, i);
            CHECK(testutil::Read(target) == fixture.patch.before[i]);
            CHECK(!testutil::Exists(target + L".apbak"));
        }
        CHECK(!blueward::HasInstallState(fixture.state));
    }
    {
        // T-086: a payload that does not match the manifest fails before any game write.
        Fixture fixture;
        CHECK(fixture.Build(L"install-unpack"));
        std::string error;
        std::string code;
        CHECK(blueward::UnpackAndVerify(fixture.plan, fixture.patch.zipPath, fixture.staging, error, &code));
        fixture.plan.files[0].sha256 = std::string(64, '0');
        error.clear();
        CHECK(!blueward::UnpackAndVerify(fixture.plan, fixture.patch.zipPath, fixture.staging, error, &code));
        CHECK(code == "E_PAYLOAD_HASH");
        CHECK(!error.empty());
    }
    {
        // T-111, T-112: a corrupt backup fails only that entry and the state keeps it for retry.
        Fixture fixture;
        CHECK(fixture.Build(L"install-restore-hash"));
        blueward::InstallReport report;
        std::wstring message;
        CHECK(blueward::ApplyInstall(fixture.plan, fixture.patch.zipPath, fixture.staging, fixture.state, fixture.backups, Options(), report,
                                     message));
        const std::wstring backup = testgame::TargetPath(fixture.patch.gameDir, 0) + L".apbak";
        CHECK(testutil::Write(backup, "corrupted backup"));
        std::string code;
        message.clear();
        CHECK(!blueward::RestoreInstall(fixture.patch.gameDir, fixture.state, fixture.backups, [] { return false; }, message, &code));
        CHECK(code == "E_RESTORE_PARTIAL");
        CHECK(message.find(L"резервная копия") != std::wstring::npos);
        CHECK(blueward::HasInstallState(fixture.state));
        // Fix the backup and retry: only the failed entry remains in the state.
        CHECK(testutil::Write(backup, fixture.patch.before[0]));
        message.clear();
        code.clear();
        CHECK(blueward::RestoreInstall(fixture.patch.gameDir, fixture.state, fixture.backups, [] { return false; }, message, &code));
        CHECK(!blueward::HasInstallState(fixture.state));
        for (std::size_t i = 0; i < 5; ++i) CHECK(testutil::Read(testgame::TargetPath(fixture.patch.gameDir, i)) == fixture.patch.before[i]);
    }
    {
        // T-113: restore refuses while the game runs.
        Fixture fixture;
        CHECK(fixture.Build(L"install-restore-running"));
        blueward::InstallReport report;
        std::wstring message;
        CHECK(blueward::ApplyInstall(fixture.plan, fixture.patch.zipPath, fixture.staging, fixture.state, fixture.backups, Options(), report,
                                     message));
        std::string code;
        message.clear();
        CHECK(!blueward::RestoreInstall(fixture.patch.gameDir, fixture.state, fixture.backups, [] { return true; }, message, &code));
        CHECK(code == "E_GAME_RUNNING");
        CHECK(message == blueward::msg::FromUtf8(blueward::msg::kGameRunning));
        CHECK(blueward::HasInstallState(fixture.state));
    }
    {
        // T-019/T-031: the offline idle gate behind the UI.
        Fixture old;
        CHECK(old.Build(L"local-gate-old", L"3075915"));
        blueward::LocalGate gate = blueward::EvaluateLocalState(old.patch.gameDir, old.state);
        CHECK(gate.state == blueward::LocalState::NeedsGameUpdate);
        CHECK(!gate.canInstall);
        CHECK(gate.detail.find(L"3075915") != std::wstring::npos);

        Fixture fixture;
        CHECK(fixture.Build(L"local-gate-ready"));
        std::vector<blueward::BaselineTarget> targets;
        for (const blueward::ResolvedFile& file : fixture.plan.files)
            targets.push_back({file.target, file.beforeSize, file.beforeSha256});
        gate = blueward::EvaluateTargetState(fixture.patch.gameDir, fixture.state, targets);
        CHECK(gate.state == blueward::LocalState::Ready);
        CHECK(gate.canInstall);
        CHECK(gate.detail.empty());

        const std::wstring missing = testgame::TargetPath(fixture.patch.gameDir, 2);
        CHECK(blueward::RemoveFile(missing));
        gate = blueward::EvaluateTargetState(fixture.patch.gameDir, fixture.state, targets);
        CHECK(gate.state == blueward::LocalState::NeedsGameUpdate);
        CHECK(!gate.canInstall);
        CHECK(gate.detail.find(blueward::BaseName(missing)) != std::wstring::npos);
        CHECK(testgame::RestoreOriginal(fixture.patch.gameDir, 2));

        blueward::InstallReport report;
        std::wstring message;
        CHECK(blueward::ApplyInstall(fixture.plan, fixture.patch.zipPath, fixture.staging, fixture.state, fixture.backups, Options(), report,
                                     message));
        gate = blueward::EvaluateLocalState(fixture.patch.gameDir, fixture.state);
        CHECK(gate.state == blueward::LocalState::Installed);
        CHECK(gate.canInstall);
    }
}
