#include "check.h"
#include "helpers.h"
#include "loopback.h"
#include "synthetic.h"

#include <string>

#include "core/feed.h"
#include "core/gamepath.h"
#include "core/installer.h"
#include "core/messages.h"
#include "core/self_update.h"
#include "ui/pages.h"

namespace {

blueward::Manifest CompatibleManifest(const blueward::Manifest& base, const std::string& launcherVersion) {
    blueward::Manifest manifest = base;
    manifest.launcher.version = launcherVersion;
    manifest.launcher.sha256 = std::string(64, 'd');
    manifest.launcher.size = 6000000;
    return manifest;
}

blueward::Manifest ForRevision(const blueward::Manifest& base, const std::string& revision) {
    blueward::Manifest manifest = base;
    manifest.patch.resourceRevision = revision;
    manifest.patch.url = "https://github.com/nightfun-pixel/ap-launcher/releases/download/patch-" + revision + "/patch-" + revision +
                         ".zip";
    return manifest;
}

const blueward::ui::UiButton* Find(const blueward::ui::UiModel& model, const std::wstring& id) {
    for (const blueward::ui::UiButton& button : model.buttons)
        if (button.id == id) return &button;
    return nullptr;
}

const blueward::ui::UiRow* FindRow(const blueward::ui::UiModel& model, const std::wstring& label) {
    for (const blueward::ui::UiRow& row : model.rows)
        if (row.label == label) return &row;
    return nullptr;
}

}  // namespace

void RunFeedTests() {
    testgame::Synthetic fixture;
    std::string error;
    CHECK(testgame::Build(testutil::Path(L"feed"), L"3219598", fixture, error));
    const blueward::Manifest latest = CompatibleManifest(fixture.manifest, "0.1.1");
    const blueward::Manifest patch = ForRevision(latest, "3273853");
    const std::string latestBody = testgame::SerializeManifest(latest);
    const std::string patchBody = testgame::SerializeManifest(patch);

    {
        // Latest is 3219598, local is 3273853: the fallback manifest-3273853.json is requested.
        loopback::Server server([&](const loopback::Request& request) {
            loopback::Reply reply;
            if (request.Path() == "/latest/download/manifest.json") {
                reply.body = latestBody;
            } else if (request.Path() == "/latest/download/manifest-3273853.json") {
                reply.body = patchBody;
            } else {
                reply.status = 404;
            }
            return reply;
        });
        blueward::WinHttpClient http;
        blueward::FeedCheck check;
        error.clear();
        const blueward::FeedStatus status =
            blueward::FetchFeed(http, "3273853", check, error, server.Url("/latest/download/manifest.json"));
        CHECK(status == blueward::FeedStatus::Available);
        CHECK(error.empty());
        CHECK(check.status == blueward::FeedStatus::Available);
        CHECK(check.launcherKnown);
        CHECK(check.launcher.version == "0.1.1");
        CHECK(check.patchManifest.patch.resourceRevision == "3273853");
        CHECK(check.patchManifest.patch.files.size() == 5);
        const std::vector<loopback::Request> requests = server.Requests();
        CHECK(requests.size() == 2);
        if (requests.size() == 2) {
            CHECK(requests[0].Path() == "/latest/download/manifest.json");
            CHECK(requests[1].Path() == "/latest/download/manifest-3273853.json");
        }
    }
    {
        // The fallback 404 is the no-patch state, not a network failure; the launcher update stays offered.
        loopback::Server server([&](const loopback::Request& request) {
            loopback::Reply reply;
            if (request.Path() == "/latest/download/manifest.json") {
                reply.body = latestBody;
            } else {
                reply.status = 404;
            }
            return reply;
        });
        blueward::WinHttpClient http;
        blueward::FeedCheck check;
        error.clear();
        const blueward::FeedStatus status =
            blueward::FetchFeed(http, "3273853", check, error, server.Url("/latest/download/manifest.json"));
        CHECK(status == blueward::FeedStatus::NoPatch);
        CHECK(error.empty());
        CHECK(check.status == blueward::FeedStatus::NoPatch);
        CHECK(check.launcherKnown);
        CHECK(check.launcher.version == "0.1.1");
        CHECK(check.patchManifest.patch.files.empty());
        CHECK(blueward::ShouldUpdate("0.1.0", check.launcher));

        // Nothing was written to the game while resolving the missing patch.
        for (std::size_t i = 0; i < fixture.before.size(); ++i)
            CHECK(testutil::Read(testgame::TargetPath(fixture.gameDir, i)) == fixture.before[i]);
        CHECK(!blueward::HasInstallState(testutil::Path(L"feed-state")));

        blueward::ui::AppView view;
        view.hasGame = true;
        view.gameVersion = L"1.0.2";
        view.resourceRevision = L"3273853";
        view.localState = blueward::LocalState::Ready;
        view.canInstall = false;
        view.feedChecked = true;
        view.patchAvailable = false;
        view.noPatch = true;
        view.launcherUpdate = blueward::ShouldUpdate("0.1.0", check.launcher);
        view.latestLauncher = L"0.1.1";
        const blueward::ui::UiModel model = blueward::ui::BuildMain(view);
        CHECK(model.status == blueward::msg::NoPatch(L"3273853"));
        CHECK(model.subtitle == L"Клиент 1.0.2 · Ресурсы 3273853");
        const blueward::ui::UiButton* install = Find(model, L"install");
        CHECK(install != nullptr);
        CHECK(install && !install->enabled);
        const blueward::ui::UiButton* update = Find(model, L"update");
        CHECK(update != nullptr);
        CHECK(update && update->enabled);
        const blueward::ui::UiRow* state = FindRow(model, L"Проверка");
        CHECK(state != nullptr);
        CHECK(state && state->value == L"Патч не выпущен для 3273853");
    }
    {
        // When the latest manifest already names the local revision, no fallback is requested.
        const blueward::Manifest matching = ForRevision(latest, "3273853");
        const std::string matchingBody = testgame::SerializeManifest(matching);
        loopback::Server server([&](const loopback::Request& request) {
            loopback::Reply reply;
            if (request.Path() == "/latest/download/manifest.json") reply.body = matchingBody;
            else reply.status = 404;
            return reply;
        });
        blueward::WinHttpClient http;
        blueward::FeedCheck check;
        error.clear();
        const blueward::FeedStatus status =
            blueward::FetchFeed(http, "3273853", check, error, server.Url("/latest/download/manifest.json"));
        CHECK(status == blueward::FeedStatus::Available);
        CHECK(check.patchManifest.patch.resourceRevision == "3273853");
        CHECK(server.Count() == 1);
    }
    {
        // A server error is not mistaken for the no-patch state.
        loopback::Server server([](const loopback::Request&) {
            loopback::Reply reply;
            reply.status = 503;
            return reply;
        });
        blueward::WinHttpClient http;
        blueward::FeedCheck check;
        error.clear();
        const blueward::FeedStatus status =
            blueward::FetchFeed(http, "3273853", check, error, server.Url("/latest/download/manifest.json"));
        CHECK(status == blueward::FeedStatus::Error);
        CHECK(!error.empty());
    }
}
