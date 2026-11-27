#pragma once
// Custom window messages. Values start at WM_APP so they cannot collide with a system message.

#include <windows.h>

namespace blueward {

inline constexpr UINT kMsgActivate = WM_APP + 20;  // another instance asks this one to come forward
inline constexpr UINT kMsgJobDone = WM_APP + 21;   // the worker posts a JobResult*
inline constexpr UINT kMsgCheckDone = WM_APP + 22; // the silent feed check finished

}  // namespace blueward
