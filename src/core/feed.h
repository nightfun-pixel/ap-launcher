#pragma once
// The revision-aware update feed. The latest manifest always carries the launcher block; when its
// patch names another revision, the launcher asks for manifest-<localRevision>.json beside it.
// A missing per-revision manifest is a normal no-patch state, not a network failure.

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
// empty and the launcher block from the latest manifest is still reported.
FeedStatus FetchFeed(HttpClient& http, std::string_view localRevision, FeedCheck& out, std::string& error,
                     std::string_view latestUrl = kFeedUrl);

}  // namespace blueward
