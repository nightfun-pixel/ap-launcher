#pragma once
// The launcher log: one line per event, `%LOCALAPPDATA%\BLUEWARD\logs\launcher.log`, rotated once at
// 4 MiB into launcher.log.1. Workers and the window both write; every call takes one lock, so lines
// never interleave. No secrets and no query strings are written.

#include <cstdint>
#include <string>

namespace blueward {

enum class LogLevel { Info, Warning, Error };

bool LogOpen(const std::wstring& path, std::uint64_t capBytes = 4ull << 20);
void LogClose();
bool LogIsOpen();
std::wstring LogFilePath();

void LogWrite(LogLevel level, const std::wstring& text);
void LogInfo(const std::wstring& text);
void LogWarn(const std::wstring& text);
void LogError(const std::wstring& text);
// Removes control characters so one message stays one line.
std::wstring SanitizeLogText(const std::wstring& text);

}  // namespace blueward
