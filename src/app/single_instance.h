#pragma once
// One named mutex per user session. A second start activates the existing window and exits.

#include <windows.h>

#include <string>

namespace blueward {

class SingleInstance {
public:
    SingleInstance() = default;
    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;
    ~SingleInstance();

    // False with error "ERROR_ALREADY_EXISTS" when another instance owns the name.
    bool Acquire(const std::wstring& name, std::string& error);
    void Release();
    bool Owned() const { return mutex_ != nullptr; }

private:
    HANDLE mutex_ = nullptr;
};

// Finds a window of `className`, restores it, brings it forward and posts `message`.
bool ActivateExistingWindow(const wchar_t* className, unsigned message);

}  // namespace blueward
