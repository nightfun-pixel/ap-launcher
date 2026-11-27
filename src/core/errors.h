#pragma once
// One sentence for a number. Russian first, because the launcher is Russian.

#include <string>

namespace blueward {

std::wstring DescribeWin32(unsigned long code);
std::wstring DescribeHttp(int status);
std::wstring DescribeHresult(long hr);
// Cuts a long path to its last two components so a message stays readable.
std::wstring ShortenPath(const std::wstring& path, std::size_t maxLength = 72);
// "Системная ошибка 5: отказано в доступе." and the like.
std::wstring Win32Sentence(unsigned long code);

}  // namespace blueward
