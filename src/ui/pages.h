#pragma once
// Pure page model. BuildMain/BuildSettings read an AppView and return rows and buttons; no window,
// no Direct2D, no COM. The layout below turns that model into rectangles that both the renderer and
// the hit test use, so a click can never disagree with what is drawn.

#include <string>
#include <vector>

#include "core/baseline_state.h"

namespace blueward::ui {

enum class Page { Main, Settings };

enum class Job { None, Checking, Installing, Restoring, Updating };

struct AppView {
    Page page = Page::Main;
    bool hasGame = false;
    std::wstring gamePath;
    std::wstring gameVersion;
    std::wstring resourceRevision;
    std::wstring requiredRevision;  // from the feed, e.g. 3219598
    bool installed = false;
    std::wstring installedVersion;
    std::wstring installedAt;
    bool updateAvailable = false;
    std::wstring latestPatch;
    bool launcherUpdate = false;
    std::wstring latestLauncher;
    bool unfinished = false;
    Job job = Job::None;
    double progress = 0.0;  // 0..1
    std::wstring status;
    std::wstring error;
    std::wstring errorCode;
    std::wstring telegram;
    std::wstring launcherVersion;
    bool video = true;
    bool checkUpdates = true;
    bool canInstall = true;
    LocalState localState = LocalState::NoGame;
    std::wstring gateDetail;
    std::wstring gateCode;
};

struct UiRow {
    std::wstring kind;  // "path", "text", "version", "link", "toggle", "about"
    std::wstring label;
    std::wstring value;
    bool emphasis = false;
};

struct UiButton {
    std::wstring id;
    std::wstring label;
    bool primary = false;
    bool enabled = true;
};

struct UiModel {
    Page page = Page::Main;
    std::wstring title;
    std::wstring subtitle;
    std::wstring badge;
    std::vector<UiRow> rows;
    std::vector<UiButton> buttons;
    bool showProgress = false;
    double progress = 0.0;
    std::wstring percent;
    std::wstring status;
    std::wstring error;
    std::wstring errorCode;
    std::wstring footer;
    std::vector<std::wstring> links;  // labels of secondary links, e.g. Telegram
};

bool IsBusy(Job job);
UiModel BuildMain(const AppView& view);
UiModel BuildSettings(const AppView& view);

// "42 %" from 0.42; clamped to 0..100.
std::wstring FormatPercent(double progress);

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    bool Contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

struct PlacedButton {
    std::wstring id;
    Rect rect;
    bool primary = false;
    bool enabled = true;
};

struct PlacedRow {
    Rect rect;
    std::size_t index = 0;
};

struct Layout {
    Rect header;
    Rect card;
    Rect title;
    Rect progress;
    std::vector<PlacedRow> rows;
    std::vector<PlacedButton> buttons;
    std::vector<PlacedButton> headerButtons;  // telegram, settings, minimize, close
    float contentTop = 0;
    float contentBottom = 0;
};

Layout ComputeLayout(const UiModel& model, float width, float height);

}  // namespace blueward::ui
