#pragma once
// The launcher's local, offline view of the game tree. The manifest remains authoritative for an
// install; this enum only drives the idle UI and the enabled state of the install button.

#include <string>

namespace blueward {

enum class LocalState { NoGame, NeedsGameUpdate, Ready, Installed };

struct LocalGate {
    LocalState state = LocalState::NoGame;
    bool canInstall = false;
    std::string code;
    std::wstring detail;
};

}  // namespace blueward
