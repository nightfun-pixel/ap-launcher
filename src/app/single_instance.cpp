#include "app/single_instance.h"

#include "core/text.h"

namespace blueward {

SingleInstance::~SingleInstance() { Release(); }

void SingleInstance::Release() {
    if (mutex_) {
        ReleaseMutex(mutex_);
        CloseHandle(mutex_);
        mutex_ = nullptr;
    }
}

bool SingleInstance::Acquire(const std::wstring& name, std::string& error) {
    error.clear();
    if (mutex_) return true;
    SetLastError(ERROR_SUCCESS);
    HANDLE mutex = CreateMutexW(nullptr, TRUE, name.c_str());
    if (!mutex) {
        error = "CreateMutex failed (" + std::to_string(GetLastError()) + ")";
        return false;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex);
        error = "ERROR_ALREADY_EXISTS";
        return false;
    }
    mutex_ = mutex;
    return true;
}

bool ActivateExistingWindow(const wchar_t* className, unsigned message) {
    if (!className) return false;
    HWND hwnd = FindWindowW(className, nullptr);
    if (!hwnd) return false;
    if (IsIconic(hwnd)) ShowWindowAsync(hwnd, SW_RESTORE);
    else ShowWindowAsync(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    return PostMessageW(hwnd, message, 0, 0) != FALSE;
}

}  // namespace blueward
