#pragma once
// UTF-8 <-> UTF-16 conversions. The launcher keeps file formats (manifest, journal, config) in UTF-8
// and talks to Windows in UTF-16.

#include <string>
#include <string_view>

namespace blueward {

std::wstring Widen(std::string_view utf8);
std::string Narrow(std::wstring_view utf16);

}  // namespace blueward
