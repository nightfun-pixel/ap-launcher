#pragma once
// The single table of user-visible Russian messages and stable error codes (SPEC FR-7, FR-82).
// No other module invents a user-facing sentence; it asks for a code's text here.

#include <string>

namespace blueward::msg {

inline constexpr const char* kRestoreNone = "Оригинал уже возвращён.";
inline constexpr const char* kAlreadyInstalled = "Уже установлено";
inline constexpr const char* kGameRunning = "Закройте игру: AzurPromiliaJP.exe уже запущен.";
inline constexpr const char* kLauncherRunning = "Закройте официальный лаунчер: AZUPRO_JP_Gamelauncher.exe уже запущен.";
inline constexpr const char* kAbout = "Неофициальный фанатский инструмент. Не связан с Yostar или Gryphline.";
inline constexpr const char* kGameInvalid = "В выбранной папке нет AzurPromiliaJP.exe и AzurPromiliaJP_Data\\StreamingAssets\\.res.";
inline constexpr const char* kIdleReady = "Готово к установке";
inline constexpr const char* kIdleNoGame = "Игра не найдена";
inline constexpr const char* kIdleInstalled = "Перевод установлен";
inline constexpr const char* kRevisionUnknown = "Ревизия ресурсов не определена — запустите игру и пройдите загрузку на первом экране.";
inline constexpr const char* kNeedsGameUpdate = "Ревизия ресурсов не определена — пройдите загрузку на первом экране";
inline constexpr const char* kChecking = "Проверяем обновления…";
inline constexpr const char* kPreflight = "Проверяем файлы игры…";
inline constexpr const char* kDownloading = "Скачиваем патч…";
inline constexpr const char* kVerifyingArchive = "Проверяем архив…";
inline constexpr const char* kExtracting = "Распаковываем…";
inline constexpr const char* kVerifyingPayloads = "Проверяем файлы перевода…";
inline constexpr const char* kBackingUp = "Создаём резервные копии…";
inline constexpr const char* kApplying = "Устанавливаем перевод…";
inline constexpr const char* kCommitting = "Завершаем установку…";
inline constexpr const char* kDone = "Готово. Запустите игру.";
inline constexpr const char* kRollingBack = "Откатываем изменения…";
inline constexpr const char* kRolledBack = "Установка отменена. Все файлы игры восстановлены.";
inline constexpr const char* kRollbackFailed = "Откат выполнен не полностью…";
inline constexpr const char* kRestoring = "Возвращаем оригинальные файлы…";
inline constexpr const char* kRestored = "Оригинальные файлы восстановлены.";
inline constexpr const char* kRestorePartial = "Восстановлены не все файлы…";
inline constexpr const char* kSelfChecking = "Проверяем обновление BLUEWARD…";
inline constexpr const char* kSelfDownloading = "Скачиваем BLUEWARD…";
inline constexpr const char* kSelfVerifying = "Проверяем BLUEWARD…";
inline constexpr const char* kSelfHandoff = "Перезапускаем BLUEWARD…";
inline constexpr const char* kUnfinished = "Незавершённая установка";
inline constexpr const char* kUnknown = "неизвестна";

inline std::wstring FromUtf8(const char* text) {
    if (!text) return {};
    std::wstring out;
    // ASCII and the Russian alphabet are enough for this table; the caller may use core/text.h
    // for anything else. This avoids a dependency cycle with the logging module.
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(text);
    std::size_t i = 0;
    while (bytes[i]) {
        unsigned cp = 0;
        int extra = 0;
        if (bytes[i] < 0x80) {
            cp = bytes[i];
        } else if ((bytes[i] & 0xE0) == 0xC0) {
            cp = bytes[i] & 0x1F;
            extra = 1;
        } else if ((bytes[i] & 0xF0) == 0xE0) {
            cp = bytes[i] & 0x0F;
            extra = 2;
        } else {
            cp = bytes[i] & 0x07;
            extra = 3;
        }
        ++i;
        for (int k = 0; k < extra && bytes[i]; ++k, ++i) cp = (cp << 6) | (bytes[i] & 0x3F);
        if (cp <= 0xFFFF) out.push_back(static_cast<wchar_t>(cp));
        else {
            cp -= 0x10000;
            out.push_back(static_cast<wchar_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
        }
    }
    return out;
}

inline std::wstring RevisionUpdate(const std::wstring& revision) {
    return L"Сначала обновите игру до " + revision +
           L" — запустите игру и пройдите загрузку на первом экране. Файлы не изменены.";
}
inline std::wstring NoPatch(const std::wstring& revision) {
    return revision.empty() ? L"Патч для этой ревизии ещё не выпущен" : L"Патч для ревизии " + revision + L" ещё не выпущен";
}
inline std::wstring NoPatchRow(const std::wstring& revision) {
    return revision.empty() ? L"Патч не выпущен" : L"Патч не выпущен для " + revision;
}

inline std::wstring GateDetailMissing(const std::wstring& basename) { return L"Не найден файл: " + basename + L"."; }
inline std::wstring GateDetailSize(const std::wstring& basename, unsigned long long expected, unsigned long long actual) {
    return L"Не совпадает размер: " + basename + L" (ожидалось " + std::to_wstring(expected) + L" Б, получено " + std::to_wstring(actual) + L" Б).";
}
inline std::wstring GateDetailHash(const std::wstring& basename) { return L"Не совпадает SHA-256: " + basename + L"."; }
inline std::wstring GateDetailChanged(const std::wstring& basename) { return L"Файл изменился во время установки: " + basename + L"."; }

inline std::wstring GameVersion(const std::wstring& manifestVersion, const std::wstring& localVersion) {
    return L"Патч рассчитан на игру " + manifestVersion + L", установлена версия " + localVersion + L". Обновите игру. Файлы не изменены.";
}

}  // namespace blueward::msg
