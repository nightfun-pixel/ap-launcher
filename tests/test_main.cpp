#include "check.h"

#include <windows.h>

#include <cstdio>
#include <string>

#include "core/fileio.h"

int g_checks = 0;
int g_failures = 0;
std::wstring g_tempRoot;

void RunJsonTests();
void RunSha256Tests();
void RunManifestTests();
void RunZipTests();
void RunHttpTests();
void RunDownloadTests();
void RunBackupTests();
void RunGamePathTests();
void RunConfigTests();
void RunInstallerTests();
void RunSelfUpdateTests();
void RunSingleInstanceTests();
void RunPageTests();
void RunLogTests();
void RunTokenTests();

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    g_tempRoot = std::wstring(temp) + L"blueward-tests-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    CreateDirectoryW(g_tempRoot.c_str(), nullptr);
    if (GetFileAttributesW(g_tempRoot.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::printf("cannot create %ls\n", g_tempRoot.c_str());
        return 2;
    }

    RunJsonTests();
    std::printf("json done\n");
    RunSha256Tests();
    std::printf("sha done\n");
    RunManifestTests();
    std::printf("manifest done\n");
    RunZipTests();
    std::printf("zip done\n");
    RunHttpTests();
    std::printf("http done\n");
    RunDownloadTests();
    std::printf("download done\n");
    RunBackupTests();
    std::printf("backup done\n");
    RunGamePathTests();
    std::printf("gamepath done\n");
    RunConfigTests();
    std::printf("config done\n");
    RunInstallerTests();
    std::printf("installer done\n");
    RunSelfUpdateTests();
    std::printf("selfupdate done\n");
    RunSingleInstanceTests();
    std::printf("single done\n");
    RunPageTests();
    std::printf("pages done\n");
    RunLogTests();
    std::printf("log done\n");
    RunTokenTests();
    std::printf("tokens done\n");

    blueward::DeleteTree(g_tempRoot);
    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
