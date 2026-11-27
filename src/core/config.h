#pragma once
// The launcher's own settings. They live in %LOCALAPPDATA%\BLUEWARD\config.json; no registry, no
// per-machine state. A missing or damaged file falls back to defaults and never fails a start.

#include <string>

namespace blueward {

struct WindowPlacement {
    int x = -1, y = -1;
    int w = 900, h = 620;
    bool maximized = false;
    bool valid = false;
};

struct Config {
    int schema = 1;
    std::wstring gamePath;
    std::string language = "ru";
    std::string telegramUrl = "REPLACE_ME";  // the placeholder disables the header link
    bool video = true;
    bool checkUpdates = true;
    WindowPlacement window;
};

bool IsValidTelegramUrl(std::string_view url);
bool TelegramEnabled(std::string_view url);

std::wstring DefaultStateDir();    // %LOCALAPPDATA%\BLUEWARD
std::wstring DefaultLogsDir();     // state\logs
std::wstring DefaultBackupsDir();  // state\backups
std::wstring DefaultCacheDir();    // state\cache
std::wstring DefaultStagingDir();  // state\staging
std::wstring DefaultDownloadsDir();  // state\downloads
std::wstring DefaultConfigPath();  // state\config.json

Config LoadConfig(const std::wstring& path);
bool SaveConfig(const std::wstring& path, const Config& config, std::string& error);

}  // namespace blueward
