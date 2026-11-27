#pragma once
// The revision-aware update feed. The latest manifest always carries the launcher block; when its
// patch names another revision, the launcher asks for manifest-<localRevision>.json beside it.
// A missing latest or per-revision manifest (404/410) is a normal no-patch state, not a network
// failure; transport, TLS and 5xx errors remain E_NET.

#include <string>
#include <string_view>

#include "core/http.h"
#include "core/manifest.h"

namespace blueward {

enum class FeedStatus { Available, NoPatch, Error };

struct FeedCheck {
    FeedStatus status = FeedStatus::Error;
    std::string localRevision;
    bool launcherKnown = false;
    LauncherInfo launcher;
    Manifest patchManifest;  // valid only when status == Available
    std::string errorCode;
};

// `latestUrl` is kFeedUrl in production and a loopback URL in tests. On NoPatch, `error` stays
// empty; the launcher block comes from the latest manifest when present and from the per-revision
// manifest otherwise.
FeedStatus FetchFeed(HttpClient& http, std::string_view localRevision, FeedCheck& out, std::string& error,
                     std::string_view latestUrl = kFeedUrl);

}  // namespace blueward
