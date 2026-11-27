#include "core/config.h"

#include <windows.h>

#include <cstdio>

#include "core/fileio.h"
#include "core/json.h"
#include "core/log.h"
#include "core/text.h"

namespace blueward {

namespace {

std::wstring LocalAppData() {
    wchar_t buffer[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    if (length > 0 && length < MAX_PATH) return std::wstring(buffer, length);
    const DWORD profileLength = GetEnvironmentVariableW(L"USERPROFILE", buffer, MAX_PATH);
    if (profileLength > 0 && profileLength < MAX_PATH) return std::wstring(buffer, profileLength) + L"\\AppData\\Local";
    return {};
}

std::string StringField(const Json* object, const char* key, const std::string& fallback) {
    const Json* value = object ? object->Find(key) : nullptr;
    return value && value->isString() ? value->asString() : fallback;
}

bool BoolField(const Json* object, const char* key, bool fallback) {
    const Json* value = object ? object->Find(key) : nullptr;
    return value && value->type() == Json::Type::Bool ? value->asBool() : fallback;
}

std::wstring BadName(const std::wstring& path) {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t suffix[40];
    swprintf(suffix, 40, L".bad-%04u%02u%02u-%02u%02u%02u.json", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    const std::wstring name = BaseName(path);
    if (name.size() > 5 && name.compare(name.size() - 5, 5, L".json") == 0)
        return ParentDir(path) + L"\\" + name.substr(0, name.size() - 5) + suffix;
    return path + suffix;
}

}  // namespace

bool IsValidTelegramUrl(std::string_view url) {
    const std::string prefixTme = "https://t.me/";
    const std::string prefixTelegram = "https://telegram.me/";
    std::string_view name;
    if (url.rfind(prefixTme, 0) == 0) name = url.substr(prefixTme.size());
    else if (url.rfind(prefixTelegram, 0) == 0) name = url.substr(prefixTelegram.size());
    else return false;
    if (name.empty() || name.size() > 64) return false;
    for (const char c : name) {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) return false;
    }
    return true;
}

bool TelegramEnabled(std::string_view url) { return url != "REPLACE_ME" && IsValidTelegramUrl(url); }

std::wstring DefaultStateDir() {
    const std::wstring base = LocalAppData();
    if (!base.empty()) return base + L"\\BLUEWARD";
    const std::wstring exe = ExeDir();
    return exe.empty() ? L"BLUEWARD" : exe + L"\\state";
}

std::wstring DefaultLogsDir() { return DefaultStateDir() + L"\\logs"; }
std::wstring DefaultBackupsDir() { return DefaultStateDir() + L"\\backups"; }
std::wstring DefaultCacheDir() { return DefaultStateDir() + L"\\cache"; }
std::wstring DefaultStagingDir() { return DefaultStateDir() + L"\\staging"; }
std::wstring DefaultDownloadsDir() { return DefaultStateDir() + L"\\downloads"; }
std::wstring DefaultConfigPath() { return DefaultStateDir() + L"\\config.json"; }

Config LoadConfig(const std::wstring& path) {
    Config config;
    std::string bytes;
    if (!ReadFileBytes(path, bytes, 1u << 20)) return config;
    Json document;
    std::string error;
    if (!ParseJson(bytes, document, error) || !document.isObject()) {
        const std::wstring bad = BadName(path);
        MoveFileExW(path.c_str(), bad.c_str(), MOVEFILE_REPLACE_EXISTING);
        LogWarn(L"config.json повреждён, переименован в " + bad);
        return config;
    }
    const Json* schema = document.Find("schema");
    if (schema && schema->isInteger() && schema->asInt() != 1) {
        const std::wstring bad = BadName(path);
        MoveFileExW(path.c_str(), bad.c_str(), MOVEFILE_REPLACE_EXISTING);
        LogWarn(L"config.json имеет неизвестную схему, переименован в " + bad);
        return config;
    }
    config.gamePath = Widen(StringField(&document, "game_path", ""));
    config.language = StringField(&document, "language", "ru");
    config.telegramUrl = StringField(&document, "telegram_url", StringField(&document, "telegram", "REPLACE_ME"));
    config.video = BoolField(&document, "video", true);
    config.checkUpdates = BoolField(&document, "check_updates", true);
    const Json* window = document.Find("window");
    if (window && window->isObject()) {
        const Json* x = window->Find("x");
        const Json* y = window->Find("y");
        const Json* w = window->Find("w");
        const Json* h = window->Find("h");
        const Json* maximized = window->Find("maximized");
        if (x && x->isInteger() && y && y->isInteger() && w && w->isInteger() && h && h->isInteger()) {
            config.window.x = static_cast<int>(x->asInt());
            config.window.y = static_cast<int>(y->asInt());
            config.window.w = static_cast<int>(w->asInt());
            config.window.h = static_cast<int>(h->asInt());
            config.window.maximized = maximized && maximized->type() == Json::Type::Bool && maximized->asBool();
            config.window.valid = config.window.w >= 760 && config.window.h >= 520;
        }
    }
    return config;
}

bool SaveConfig(const std::wstring& path, const Config& config, std::string& error) {
    Json document = Json::Object();
    std::string existing;
    if (ReadFileBytes(path, existing, 1u << 20)) {
        Json parsed;
        std::string parseError;
        if (ParseJson(existing, parsed, parseError) && parsed.isObject()) document = std::move(parsed);
    }
    document.Set("schema", Json::Int(1));
    document.Set("game_path", Json::String(Narrow(config.gamePath)));
    document.Set("language", Json::String(config.language));
    document.Set("telegram_url", Json::String(config.telegramUrl));
    document.Set("video", Json::Bool(config.video));
    document.Set("check_updates", Json::Bool(config.checkUpdates));
    Json window = Json::Object();
    window.Set("x", Json::Int(config.window.valid ? config.window.x : -1));
    window.Set("y", Json::Int(config.window.valid ? config.window.y : -1));
    window.Set("w", Json::Int(config.window.w));
    window.Set("h", Json::Int(config.window.h));
    window.Set("maximized", Json::Bool(config.window.maximized));
    document.Set("window", std::move(window));
    MakeDirs(ParentDir(path));
    return WriteFileAtomic(path, DumpJson(document), error);
}

}  // namespace blueward
