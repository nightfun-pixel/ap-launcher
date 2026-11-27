#include "ui/pages.h"

#include <algorithm>
#include <cmath>

#include "core/messages.h"
#include "core/version.h"
#include "ui/theme.h"

namespace blueward::ui {

bool IsBusy(Job job) { return job != Job::None; }

std::wstring FormatPercent(double progress) {
    if (!(progress > 0.0)) return L"0 %";
    const double clamped = std::min(1.0, progress);
    const int percent = static_cast<int>(std::lround(clamped * 100.0));
    return std::to_wstring(percent) + L" %";
}

UiModel BuildMain(const AppView& view) {
    UiModel model;
    model.page = Page::Main;
    model.title = L"Перевод Azur Promilia";
    model.subtitle = L"Японский клиент";
    if (!view.gameVersion.empty() || !view.resourceRevision.empty()) {
        model.subtitle = L"Клиент " + (view.gameVersion.empty() ? std::wstring(msg::FromUtf8(msg::kUnknown)) : view.gameVersion) +
                         L" · Ресурсы " + (view.resourceRevision.empty() ? std::wstring(msg::FromUtf8(msg::kUnknown)) : view.resourceRevision);
    }
    model.badge = view.installed ? msg::FromUtf8(msg::kIdleInstalled) : (view.hasGame ? msg::FromUtf8(msg::kIdleReady) : msg::FromUtf8(msg::kIdleNoGame));

    model.rows.push_back({L"path", L"Папка игры", view.hasGame ? view.gamePath : L"Не выбрана", true});
    model.rows.push_back({L"text", L"Для патча", L"нужен ресурс 3219598", false});
    model.rows.push_back({L"text", L"Перевод",
                          view.installed ? L"Установлен перевод " + view.installedVersion +
                                               (view.installedAt.empty() ? std::wstring() : L" · " + view.installedAt)
                                         : L"Перевод не установлен",
                          view.installed});
    if (view.localState == LocalState::NeedsGameUpdate && !view.gateDetail.empty())
        model.rows.push_back({L"text", L"Проверка", view.gateDetail, true});
    if (view.updateAvailable && !view.latestPatch.empty())
        model.rows.push_back({L"text", L"Обновление", L"доступно " + view.latestPatch, true});

    const bool busy = IsBusy(view.job);
    const bool gateReady = view.hasGame && view.canInstall && !busy;
    if (view.unfinished && !busy) {
        model.buttons.push_back({L"recover", L"Завершить откат", true, true});
    } else if (busy) {
        model.buttons.push_back({L"cancel", L"Отмена", true, view.job != ui::Job::Restoring && view.job != ui::Job::Updating});
    } else {
        if (view.installed && !view.updateAvailable) {
            model.buttons.push_back({L"restore", L"Вернуть оригинал", true, !busy});
            model.buttons.push_back({L"install", L"Переустановить перевод", false, gateReady});
        } else {
            model.buttons.push_back({L"install", L"Установить / Обновить перевод", true, gateReady});
            model.buttons.push_back({L"restore", L"Вернуть оригинал", false, view.installed && !busy});
        }
        if (view.launcherUpdate) model.buttons.push_back({L"update", L"Обновить BLUEWARD", false, view.canInstall});
        if (!view.error.empty()) {
            model.buttons.push_back({L"copy", L"Скопировать ошибку", false, true});
            model.buttons.push_back({L"retry", L"Повторить", false, true});
        }
    }

    model.showProgress = busy;
    model.progress = std::clamp(view.progress, 0.0, 1.0);
    model.percent = FormatPercent(view.progress);
    if (busy || !view.error.empty()) {
        model.status = view.status;
    } else {
        switch (view.localState) {
            case LocalState::NoGame: model.status = msg::FromUtf8(msg::kIdleNoGame); break;
            case LocalState::NeedsGameUpdate: model.status = msg::FromUtf8(msg::kNeedsGameUpdate); break;
            case LocalState::Ready: model.status = msg::FromUtf8(msg::kIdleReady); break;
            case LocalState::Installed: model.status = msg::FromUtf8(msg::kIdleInstalled); break;
        }
    }
    model.error = view.error;
    model.errorCode = view.errorCode;
    if (!view.telegram.empty()) model.links.push_back(L"Telegram");
    return model;
}

UiModel BuildSettings(const AppView& view) {
    UiModel model;
    model.page = Page::Settings;
    model.title = L"Настройки";
    model.subtitle = L"BLUEWARD " + (view.launcherVersion.empty() ? std::wstring(kBluewardVersionW) : view.launcherVersion);
    model.badge = view.installed ? msg::FromUtf8(msg::kIdleInstalled) : msg::FromUtf8(msg::kIdleNoGame);
    model.rows.push_back({L"path", L"Папка игры", view.hasGame ? view.gamePath : L"Не выбрана", true});
    model.rows.push_back({L"toggle", L"Видео", view.video ? L"Видео включено" : L"Видео выключено", false});
    model.rows.push_back({L"toggle", L"Обновления", view.checkUpdates ? L"Проверять при запуске" : L"Не проверять", false});
    model.rows.push_back({L"link", L"Telegram", view.telegram.empty() ? L"не задан" : view.telegram, !view.telegram.empty()});
    model.rows.push_back({L"about", L"Версия",
                          model.subtitle + (view.launcherUpdate ? L" · доступно " + view.latestLauncher : L" · последняя версия"),
                          view.launcherUpdate});
    model.footer = msg::FromUtf8(msg::kAbout);

    const bool busy = IsBusy(view.job);
    model.buttons.push_back({L"back", L"Назад", true, !busy});
    model.buttons.push_back({L"pick", L"Выбрать папку", false, !busy});
    model.buttons.push_back({L"opengame", L"Открыть папку игры", false, view.hasGame && !busy});
    model.buttons.push_back({L"toggle", view.video ? L"Выключить видео" : L"Включить видео", false, !busy});
    model.buttons.push_back({L"toggles", view.checkUpdates ? L"Не проверять обновления" : L"Проверять обновления", false, !busy});
    model.buttons.push_back({L"openlog", L"Открыть журнал", false, true});
    if (view.launcherUpdate) model.buttons.push_back({L"update", L"Обновить BLUEWARD", false, !busy});
    if (!view.telegram.empty()) model.buttons.push_back({L"telegram", L"Открыть Telegram", false, true});
    model.status = view.status;
    model.error = view.error;
    model.errorCode = view.errorCode;
    return model;
}

Layout ComputeLayout(const UiModel& model, float width, float height) {
    Layout layout;
    layout.header = {0, 0, std::max(320.0f, width), theme::kHeaderHeight};
    const bool compact = width < 1000.0f || height < 680.0f;
    const float margin = compact ? 28.0f : 48.0f;
    float cardWidth = std::min(theme::kCardWidth, width - margin * 2);
    if (cardWidth < 260.0f) cardWidth = std::max(260.0f, width - 24.0f);
    float cardX = width - cardWidth - margin;
    if (cardX < margin) cardX = margin;
    const float cardY = compact ? 76.0f : 88.0f;
    float cardHeight = std::min(height - cardY - (compact ? 24.0f : 32.0f), compact ? 516.0f : 560.0f);
    if (cardHeight < 300.0f) cardHeight = std::max(240.0f, height - cardY - 12.0f);
    layout.card = {cardX, cardY, cardWidth, cardHeight};
    layout.title = {cardX + 28, cardY + 16, cardWidth - 56, compact ? 42.0f : 50.0f};

    float y = cardY + (compact ? 80.0f : 92.0f);
    const float rowHeight = compact ? 38.0f : 42.0f;
    for (std::size_t i = 0; i < model.rows.size(); ++i) {
        layout.rows.push_back({{cardX + 28, y, cardWidth - 56, rowHeight}, i});
        y += rowHeight;
    }
    layout.contentTop = cardY + (compact ? 80.0f : 92.0f);
    layout.contentBottom = y;

    const float buttonHeight = 44.0f;
    if ((model.page == Page::Settings && model.buttons.size() > 2) || model.buttons.size() > 2) {
        // Two columns, so a settings or error page does not need a tall stack of buttons.
        const float gap = 8.0f;
        const float columnWidth = (cardWidth - 56 - gap) / 2.0f;
        const std::size_t rows = (model.buttons.size() + 1) / 2;
        const std::size_t columns = 2;
        const float gridHeight = static_cast<float>(rows) * buttonHeight + static_cast<float>(rows - 1) * gap;
        const float top = cardY + cardHeight - (compact ? 20.0f : 24.0f) - gridHeight;
        layout.buttons.resize(model.buttons.size());
        for (std::size_t i = 0; i < model.buttons.size(); ++i) {
            const std::size_t row = i / columns, column = i % columns;
            PlacedButton placed;
            placed.id = model.buttons[i].id;
            placed.primary = model.buttons[i].primary;
            placed.enabled = model.buttons[i].enabled;
            placed.rect = {cardX + 28 + static_cast<float>(column) * (columnWidth + gap), top + static_cast<float>(row) * (buttonHeight + gap),
                           columnWidth, buttonHeight};
            layout.buttons[i] = placed;
        }
    } else {
        float by = cardY + cardHeight - (compact ? 20.0f : 24.0f);
        layout.buttons.resize(model.buttons.size());
        for (int index = static_cast<int>(model.buttons.size()) - 1; index >= 0; --index) {
            by -= buttonHeight;
            PlacedButton placed;
            placed.id = model.buttons[static_cast<std::size_t>(index)].id;
            placed.primary = model.buttons[static_cast<std::size_t>(index)].primary;
            placed.enabled = model.buttons[static_cast<std::size_t>(index)].enabled;
            placed.rect = {cardX + 28, by, cardWidth - 56, buttonHeight};
            layout.buttons[static_cast<std::size_t>(index)] = placed;
            by -= 8.0f;
        }
        if (model.showProgress) {
            layout.progress = {cardX + 28, by - 26.0f, cardWidth - 56, 6.0f};
            by -= 38.0f;
        }
    }

    // Header controls: close and minimize at the far right, then Settings, then Telegram.
    float headerX = width - 40.0f;
    layout.headerButtons.push_back({L"close", {headerX, 12.0f, 26.0f, 28.0f}, false, true});
    headerX -= 34.0f;
    layout.headerButtons.push_back({L"minimize", {headerX, 12.0f, 26.0f, 28.0f}, false, true});
    headerX -= 6.0f;
    headerX -= 92.0f;
    layout.headerButtons.push_back({L"settings", {headerX, 12.0f, 88.0f, 28.0f}, false, true});
    if (!model.links.empty()) {
        headerX -= 6.0f;
        headerX -= 80.0f;
        layout.headerButtons.push_back({L"telegram", {headerX, 12.0f, 76.0f, 28.0f}, false, true});
    }
    return layout;
}

}  // namespace blueward::ui
