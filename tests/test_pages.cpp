#include "check.h"

#include "core/messages.h"
#include "ui/pages.h"
#include "ui/theme.h"

namespace {

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

const blueward::ui::PlacedButton* FindPlaced(const blueward::ui::Layout& layout, const std::wstring& id) {
    for (const blueward::ui::PlacedButton& button : layout.buttons)
        if (button.id == id) return &button;
    return nullptr;
}

}  // namespace

void RunPageTests() {
    {
        // T-019: no game folder.
        blueward::ui::AppView view;
        view.launcherVersion = L"0.1.0";
        view.localState = blueward::LocalState::NoGame;
        view.canInstall = false;
        const blueward::ui::UiModel model = blueward::ui::BuildMain(view);
        const blueward::ui::UiButton* install = Find(model, L"install");
        CHECK(install != nullptr);
        CHECK(install && !install->enabled);
        const blueward::ui::UiButton* restore = Find(model, L"restore");
        CHECK(restore != nullptr);
        CHECK(restore && !restore->enabled);
        const blueward::ui::UiRow* path = FindRow(model, L"Папка игры");
        CHECK(path != nullptr);
        CHECK(path && path->value == L"Не выбрана");
        CHECK(model.status == blueward::msg::FromUtf8(blueward::msg::kIdleNoGame));
    }
    {
        // T-019: 3075915 / missing targets -> NeedsGameUpdate and a disabled install button.
        blueward::ui::AppView view;
        view.hasGame = true;
        view.gamePath = L"D:\\YostarGames\\AZUPRO_JP";
        view.gameVersion = L"1.0.2";
        view.resourceRevision = L"3075915";
        view.localState = blueward::LocalState::NeedsGameUpdate;
        view.canInstall = false;
        view.gateDetail = L"ресурс 3075915";
        view.gateCode = L"E_VERSION_REVISION";
        const blueward::ui::UiModel model = blueward::ui::BuildMain(view);
        CHECK(model.status == blueward::msg::FromUtf8(blueward::msg::kNeedsGameUpdate));
        const blueward::ui::UiButton* install = Find(model, L"install");
        CHECK(install != nullptr);
        CHECK(install && !install->enabled);
        const blueward::ui::UiButton* restore = Find(model, L"restore");
        CHECK(restore && !restore->enabled);
        CHECK(FindRow(model, L"Проверка") != nullptr);
    }
    {
        // T-019: all five match -> Ready and an enabled install button.
        blueward::ui::AppView view;
        view.hasGame = true;
        view.gameVersion = L"1.0.2";
        view.resourceRevision = L"3273853";
        view.requiredRevision = L"3273853";
        view.latestPatch = L"2026.12.01-1";
        view.feedChecked = true;
        view.patchAvailable = true;
        view.localState = blueward::LocalState::Ready;
        view.canInstall = true;
        const blueward::ui::UiModel model = blueward::ui::BuildMain(view);
        CHECK(model.status == blueward::msg::FromUtf8(blueward::msg::kIdleReady));
        CHECK(model.subtitle == L"Клиент 1.0.2 · Ресурсы 3273853");
        const blueward::ui::UiRow* check = FindRow(model, L"Проверка");
        CHECK(check != nullptr);
        CHECK(check && check->value == L"ресурс 3273853");
        const blueward::ui::UiButton* install = Find(model, L"install");
        CHECK(install != nullptr);
        CHECK(install && install->enabled);
        CHECK(install && install->primary);
        const blueward::ui::UiButton* restore = Find(model, L"restore");
        CHECK(restore && !restore->enabled);
    }
    {
        // While the feed is in flight the state line names the revision being checked.
        blueward::ui::AppView view;
        view.hasGame = true;
        view.gameVersion = L"1.0.2";
        view.resourceRevision = L"3273853";
        view.localState = blueward::LocalState::Ready;
        view.canInstall = true;
        view.feedChecking = true;
        const blueward::ui::UiModel model = blueward::ui::BuildMain(view);
        CHECK(model.status == L"Проверка: ресурс 3273853");
        CHECK(FindRow(model, L"Проверка") != nullptr);
    }
    {
        // A local revision without a published manifest disables install and keeps self-update alive.
        blueward::ui::AppView view;
        view.hasGame = true;
        view.gameVersion = L"1.0.2";
        view.resourceRevision = L"3273853";
        view.localState = blueward::LocalState::Ready;
        view.canInstall = false;
        view.feedChecked = true;
        view.patchAvailable = false;
        view.noPatch = true;
        view.launcherUpdate = true;
        view.latestLauncher = L"0.1.1";
        const blueward::ui::UiModel model = blueward::ui::BuildMain(view);
        CHECK(model.status == blueward::msg::NoPatch(L"3273853"));
        const blueward::ui::UiRow* check = FindRow(model, L"Проверка");
        CHECK(check != nullptr);
        CHECK(check && check->value == L"Патч не выпущен для 3273853");
        const blueward::ui::UiButton* install = Find(model, L"install");
        CHECK(install != nullptr);
        CHECK(install && !install->enabled);
        const blueward::ui::UiButton* update = Find(model, L"update");
        CHECK(update != nullptr);
        CHECK(update && update->enabled);
        CHECK(model.subtitle.find(L"3219598") == std::wstring::npos);
        CHECK(model.status.find(L"3219598") == std::wstring::npos);
    }
    {
        // T-019: an installed translation -> Installed and an enabled restore button.
        blueward::ui::AppView view;
        view.hasGame = true;
        view.gameVersion = L"1.0.2";
        view.resourceRevision = L"3273853";
        view.localState = blueward::LocalState::Installed;
        view.canInstall = true;
        view.installed = true;
        view.installedVersion = L"2026.11.27-1";
        view.installedAt = L"2026-11-27T20:00:00Z";
        const blueward::ui::UiModel model = blueward::ui::BuildMain(view);
        CHECK(model.status == blueward::msg::FromUtf8(blueward::msg::kIdleInstalled));
        const blueward::ui::UiButton* restore = Find(model, L"restore");
        CHECK(restore != nullptr);
        CHECK(restore && restore->enabled);
        CHECK(restore && restore->primary);
    }
    {
        // T-033: installed and current -> restore is primary.
        blueward::ui::AppView view;
        view.hasGame = true;
        view.gamePath = L"D:\\YostarGames\\AZUPRO_JP";
        view.gameVersion = L"1.0.2";
        view.resourceRevision = L"3273853";
        view.requiredRevision = L"3273853";
        view.installed = true;
        view.installedVersion = L"2026.11.27-1";
        view.installedAt = L"2026-11-27T20:00:00Z";
        view.launcherVersion = L"0.1.0";
        view.localState = blueward::LocalState::Installed;
        view.canInstall = true;
        const blueward::ui::UiModel model = blueward::ui::BuildMain(view);
        const blueward::ui::UiButton* restore = Find(model, L"restore");
        const blueward::ui::UiButton* install = Find(model, L"install");
        CHECK(restore && restore->primary);
        CHECK(install && !install->primary);
        const blueward::ui::UiRow* state = FindRow(model, L"Перевод");
        CHECK(state != nullptr);
        CHECK(state && state->value.find(L"Установлен перевод") != std::wstring::npos);
        CHECK(state && state->value.find(L"2026-11-27") != std::wstring::npos);
    }
    {
        // T-020: progress, percent and the error card.
        blueward::ui::AppView view;
        view.hasGame = true;
        view.job = blueward::ui::Job::Installing;
        view.progress = 0.42;
        view.status = L"Скачиваем патч…";
        view.error = L"Сервер ответил HTTP 503";
        view.errorCode = L"E_NET";
        const blueward::ui::UiModel model = blueward::ui::BuildMain(view);
        CHECK(model.showProgress);
        CHECK(model.percent == L"42 %");
        CHECK(model.error == view.error);
        CHECK(model.errorCode == L"E_NET");
        CHECK(Find(model, L"cancel") != nullptr);
        CHECK(Find(model, L"copy") == nullptr);  // the error buttons appear only when idle
    }
    {
        CHECK(blueward::ui::FormatPercent(0.0) == L"0 %");
        CHECK(blueward::ui::FormatPercent(-1.0) == L"0 %");
        CHECK(blueward::ui::FormatPercent(0.4249) == L"42 %");
        CHECK(blueward::ui::FormatPercent(1.0) == L"100 %");
        CHECK(blueward::ui::FormatPercent(2.0) == L"100 %");
    }
    {
        // T-021: every settings row from FR-70, plus the about text.
        blueward::ui::AppView view;
        view.page = blueward::ui::Page::Settings;
        view.launcherVersion = L"0.1.0";
        view.telegram = L"https://t.me/example";
        view.checkUpdates = true;
        const blueward::ui::UiModel model = blueward::ui::BuildSettings(view);
        CHECK(Find(model, L"back") != nullptr);
        CHECK(Find(model, L"pick") != nullptr);
        CHECK(Find(model, L"openlog") != nullptr);
        CHECK(Find(model, L"toggles") != nullptr);
        CHECK(FindRow(model, L"Версия") != nullptr);
        CHECK(FindRow(model, L"Telegram") != nullptr);
        CHECK(FindRow(model, L"Видео") != nullptr);
        CHECK(FindRow(model, L"Обновления") != nullptr);
        CHECK(model.footer == blueward::msg::FromUtf8(blueward::msg::kAbout));
    }
    {
        // T-019: an unfinished journal offers the recovery action.
        blueward::ui::AppView view;
        view.hasGame = true;
        view.unfinished = true;
        const blueward::ui::UiModel model = blueward::ui::BuildMain(view);
        CHECK(Find(model, L"recover") != nullptr);
    }
    {
        // The error state gives the long primary action a full-width row at 1100x720.
        blueward::ui::AppView view;
        view.hasGame = true;
        view.gamePath = L"D:\\YostarGames\\AZUPRO_JP";
        view.gameVersion = L"1.0.2";
        view.resourceRevision = L"3273853";
        view.localState = blueward::LocalState::Ready;
        view.canInstall = true;
        view.feedChecked = true;
        view.error = L"Сервер ответил HTTP 500";
        view.errorCode = L"E_NET";
        const blueward::ui::UiModel model = blueward::ui::BuildMain(view);
        const blueward::ui::UiButton* install = Find(model, L"install");
        CHECK(install != nullptr);
        CHECK(model.buttons.size() == 4);
        const blueward::ui::Layout layout = blueward::ui::ComputeLayout(model, 1100.0f, 720.0f);
        const blueward::ui::PlacedButton* placed = FindPlaced(layout, L"install");
        CHECK(placed != nullptr);
        if (install && placed) {
            CHECK(placed->primary);
            CHECK(placed->rect.w == layout.card.w - 56.0f);
            CHECK(placed->rect.w > 400.0f);
            // 14.5 px Segoe UI Semibold averages well under 9 px per Cyrillic glyph; 9.5 is a
            // conservative bound for the 29-character label, so the width proves it is not clipped.
            CHECK(static_cast<float>(install->label.size()) * 9.5f < placed->rect.w);
            for (const blueward::ui::PlacedButton& other : layout.buttons) {
                if (other.id == placed->id) continue;
                CHECK(!(other.rect.x < placed->rect.x + placed->rect.w && other.rect.x + other.rect.w > placed->rect.x &&
                        other.rect.y < placed->rect.y + placed->rect.h && other.rect.y + other.rect.h > placed->rect.y));
            }
        }
    }
    {
        // T-018: default and minimum geometry.
        volatile float defaultWidth = blueward::theme::kDefaultWidth;
        volatile float defaultHeight = blueward::theme::kDefaultHeight;
        volatile float minimumWidth = blueward::theme::kMinWidth;
        volatile float minimumHeight = blueward::theme::kMinHeight;
        volatile float headerHeight = blueward::theme::kHeaderHeight;
        CHECK(defaultWidth == 900.0f);
        CHECK(defaultHeight == 620.0f);
        CHECK(minimumWidth == 760.0f);
        CHECK(minimumHeight == 520.0f);
        CHECK(headerHeight == 56.0f);
        blueward::ui::UiModel model;
        model.title = L"Перевод Azur Promilia";
        model.buttons.push_back({L"install", L"Установить / Обновить перевод", true, true});
        model.buttons.push_back({L"restore", L"Вернуть оригинал", false, true});
        model.showProgress = true;
        const blueward::ui::Layout layout = blueward::ui::ComputeLayout(model, 900.0f, 620.0f);
        CHECK(layout.buttons.size() == 2);
        if (layout.buttons.size() == 2) {
            CHECK(layout.buttons[0].rect.w > 100.0f);
            CHECK(layout.buttons[0].rect.x >= layout.card.x);
            CHECK(layout.buttons[0].rect.y > layout.card.y);
            CHECK(layout.card.x + layout.card.w <= 900.0f);
            CHECK(layout.progress.h > 0.0f);
            CHECK(layout.card.Contains(layout.buttons[0].rect.x + 1, layout.buttons[0].rect.y + 1));
        }
        const blueward::ui::Layout minimumLayout = blueward::ui::ComputeLayout(model, 760.0f, 520.0f);
        CHECK(minimumLayout.card.w <= 760.0f);
        CHECK(minimumLayout.card.x >= 0.0f);
    }
}
