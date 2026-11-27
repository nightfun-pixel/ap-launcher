#include "check.h"
#include "helpers.h"

#include <string>

#include "core/config.h"

void RunConfigTests() {
    {
        // T-013: missing file gives defaults.
        const blueward::Config defaults = blueward::LoadConfig(testutil::Path(L"config-missing\\config.json"));
        CHECK(defaults.gamePath.empty());
        CHECK(defaults.language == "ru");
        CHECK(defaults.video);
        CHECK(defaults.telegramUrl == "REPLACE_ME");
        CHECK(defaults.window.w == 900);
        CHECK(defaults.window.h == 620);
    }
    {
        // T-013: round-trip, unknown keys preserved, atomic write.
        const std::wstring path = testutil::Path(L"config-round\\config.json");
        CHECK(testutil::Write(path, "{\"unknown_key\": 42}\n"));
        blueward::Config config;
        config.gamePath = L"D:\\YostarGames\\AZUPRO_JP";
        config.language = "ru";
        config.telegramUrl = "https://t.me/blueward_test";
        config.video = false;
        config.checkUpdates = false;
        config.window.x = 100;
        config.window.y = 80;
        config.window.w = 1000;
        config.window.h = 700;
        config.window.valid = true;
        std::string error;
        CHECK(blueward::SaveConfig(path, config, error));
        CHECK(error.empty());
        const blueward::Config loaded = blueward::LoadConfig(path);
        CHECK(loaded.gamePath == config.gamePath);
        CHECK(loaded.telegramUrl == config.telegramUrl);
        CHECK(!loaded.video);
        CHECK(!loaded.checkUpdates);
        CHECK(loaded.window.valid);
        CHECK(loaded.window.w == 1000);
        CHECK(testutil::Read(path).find("unknown_key") != std::string::npos);
        CHECK(!testutil::Exists(path + L".tmp"));
    }
    {
        // T-013: a damaged file is renamed, defaults are used, no throw.
        const std::wstring path = testutil::Path(L"config-broken\\config.json");
        CHECK(testutil::Write(path, "{ this is not json"));
        const blueward::Config loaded = blueward::LoadConfig(path);
        CHECK(loaded.language == "ru");
        CHECK(loaded.gamePath.empty());
        CHECK(loaded.video);
        bool renamed = false;
        for (const std::wstring& file : blueward::ListFiles(testutil::Path(L"config-broken"))) {
            if (file.find(L"config.bad-") != std::wstring::npos) renamed = true;
        }
        CHECK(renamed);
    }
    {
        // T-014: only the two HTTPS hosts with a 1..64 [A-Za-z0-9_] name are accepted.
        CHECK(blueward::IsValidTelegramUrl("https://t.me/blueward_test"));
        CHECK(blueward::IsValidTelegramUrl("https://telegram.me/Name123"));
        CHECK(!blueward::IsValidTelegramUrl("REPLACE_ME"));
        CHECK(!blueward::IsValidTelegramUrl("http://t.me/name"));
        CHECK(!blueward::IsValidTelegramUrl("https://example.com/name"));
        CHECK(!blueward::IsValidTelegramUrl("https://t.me/name/extra"));
        CHECK(!blueward::IsValidTelegramUrl("https://t.me/"));
        CHECK(!blueward::IsValidTelegramUrl("https://t.me/name?x=1"));
        CHECK(!blueward::IsValidTelegramUrl("https://t.me/имя"));
        CHECK(!blueward::IsValidTelegramUrl("https://t.me/.."));
        CHECK(!blueward::IsValidTelegramUrl("https://t.me/" + std::string(65, 'a')));
        CHECK(blueward::TelegramEnabled("https://t.me/blueward_test"));
        CHECK(!blueward::TelegramEnabled("REPLACE_ME"));
    }
    {
        CHECK(!blueward::DefaultStateDir().empty());
        CHECK(blueward::DefaultStateDir().find(L"BLUEWARD") != std::wstring::npos);
        CHECK(blueward::DefaultBackupsDir().find(L"backups") != std::wstring::npos);
        CHECK(blueward::DefaultLogsDir().find(L"logs") != std::wstring::npos);
        CHECK(blueward::DefaultStagingDir().find(L"staging") != std::wstring::npos);
    }
}
