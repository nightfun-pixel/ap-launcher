#include "core/feed.h"

#include <utility>

namespace blueward {

namespace {

bool Digits(std::string_view value) {
    if (value.empty() || value.size() > 12) return false;
    for (const char c : value)
        if (c < '0' || c > '9') return false;
    return true;
}

std::string SiblingManifest(std::string_view latestUrl, std::string_view revision) {
    if (!Digits(revision)) return {};
    const std::string suffix = "/manifest.json";
    if (latestUrl.size() <= suffix.size() || latestUrl.compare(latestUrl.size() - suffix.size(), suffix.size(), suffix) != 0)
        return {};
    return std::string(latestUrl.substr(0, latestUrl.size() - suffix.size())) + "/manifest-" + std::string(revision) + ".json";
}

bool FetchJson(HttpClient& http, const std::string& url, std::string& body, int& status, std::string& error) {
    body.clear();
    status = 0;
    error.clear();
    std::string stopped;
    const HttpResult result = http.Get(
        url, {{"Accept", "application/json"}},
        [&](const HttpResult& headers) {
            status = headers.status;
            if (headers.hasContentLength && headers.contentLength > kManifestLimit) {
                stopped = "the response is larger than the manifest limit";
                return false;
            }
            return true;
        },
        [&](const void* data, std::size_t size) {
            if (body.size() + size > kManifestLimit) {
                stopped = "the response is larger than the manifest limit";
                return false;
            }
            body.append(static_cast<const char*>(data), size);
            return true;
        });
    if (!stopped.empty()) {
        error = stopped;
        return false;
    }
    if (!result.completed) {
        error = result.error.empty() ? "the request failed" : result.error;
        return false;
    }
    if (result.status != 200) {
        error = "the server answered HTTP " + std::to_string(result.status);
        return false;
    }
    return true;
}

}  // namespace

FeedStatus FetchFeed(HttpClient& http, std::string_view localRevision, FeedCheck& out, std::string& error,
                     std::string_view latestUrl) {
    out = {};
    out.localRevision = std::string(localRevision);
    error.clear();

    std::string body;
    int status = 0;
    bool latestMissing = false;
    if (!FetchJson(http, std::string(latestUrl), body, status, error)) {
        if (status == 404 || status == 410) {
            // A release without the default manifest still serves manifest-<revision>.json; this is
            // a missing file, not a transport failure.
            error.clear();
            latestMissing = true;
        } else {
            out.errorCode = "E_NET";
            out.status = FeedStatus::Error;
            return out.status;
        }
    }

    Manifest latest;
    std::string parseError;
    if (!latestMissing) {
        if (!ParseManifest(body, latest, parseError, &out.errorCode)) {
            error = "manifest rejected: " + parseError;
            out.status = FeedStatus::Error;
            return out.status;
        }
        out.launcherKnown = true;
        out.launcher = latest.launcher;
    }

    if (!Digits(localRevision)) {
        out.status = FeedStatus::NoPatch;
        return out.status;
    }
    if (!latestMissing && latest.patch.resourceRevision == localRevision) {
        out.patchManifest = std::move(latest);
        out.status = FeedStatus::Available;
        return out.status;
    }

    const std::string fallbackUrl = SiblingManifest(latestUrl, localRevision);
    if (fallbackUrl.empty()) {
        out.errorCode = "E_MANIFEST_SCHEMA";
        error = "the latest manifest URL does not end in /manifest.json";
        out.status = FeedStatus::Error;
        return out.status;
    }
    body.clear();
    status = 0;
    if (!FetchJson(http, fallbackUrl, body, status, error)) {
        if (status == 404 || status == 410) {
            error.clear();
            out.status = FeedStatus::NoPatch;
            return out.status;
        }
        out.errorCode = "E_NET";
        out.status = FeedStatus::Error;
        return out.status;
    }

    Manifest patch;
    if (!ParseManifest(body, patch, parseError, &out.errorCode)) {
        error = "manifest rejected: " + parseError;
        out.status = FeedStatus::Error;
        return out.status;
    }
    if (!out.launcherKnown) {
        out.launcherKnown = true;
        out.launcher = patch.launcher;
    } else if (CompareVersions(patch.launcher.version, out.launcher.version) > 0) {
        out.launcher = patch.launcher;
    }
    if (patch.patch.resourceRevision != localRevision) {
        out.errorCode = "E_MANIFEST_REVISION";
        error = "manifest-" + std::string(localRevision) + ".json names revision " + patch.patch.resourceRevision;
        out.status = FeedStatus::Error;
        return out.status;
    }
    out.patchManifest = std::move(patch);
    out.status = FeedStatus::Available;
    return out.status;
}

}  // namespace blueward
