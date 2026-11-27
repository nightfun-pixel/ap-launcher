#include "core/errors.h"

#include <windows.h>

#include <cstdio>

namespace blueward {

std::wstring Win32Sentence(unsigned long code) {
    switch (code) {
        case ERROR_FILE_NOT_FOUND: return L"файл не найден";
        case ERROR_PATH_NOT_FOUND: return L"папка не найдена";
        case ERROR_ACCESS_DENIED: return L"отказано в доступе";
        case ERROR_SHARING_VIOLATION: return L"файл занят другой программой";
        case ERROR_LOCK_VIOLATION: return L"файл заблокирован";
        case ERROR_DISK_FULL:
        case ERROR_HANDLE_DISK_FULL: return L"на диске нет места";
        case ERROR_FILENAME_EXCED_RANGE: return L"слишком длинное имя файла";
        case ERROR_CANCELLED: return L"действие отменено";
        case ERROR_INVALID_PARAMETER: return L"неверный параметр";
        case ERROR_ALREADY_EXISTS: return L"файл уже существует";
        case ERROR_DIRECTORY: return L"неверное имя папки";
        case ERROR_FILE_CORRUPT: return L"файл повреждён";
        case ERROR_WRITE_PROTECT: return L"носитель защищён от записи";
        case ERROR_NOT_ENOUGH_MEMORY: return L"не хватает памяти";
        case ERROR_MOD_NOT_FOUND: return L"не найден модуль";
        case ERROR_PROC_NOT_FOUND: return L"не найдена функция";
        default: return L"неизвестная ошибка";
    }
}

std::wstring Win32Sentence(unsigned long code);

std::wstring DescribeWin32(unsigned long code) {
    return L"Ошибка Windows " + std::to_wstring(code) + L": " + Win32Sentence(code) + L".";
}

std::wstring DescribeHttp(int status) {
    switch (status) {
        case 400: return L"Сервер отклонил запрос (HTTP 400).";
        case 401: return L"Сервер требует авторизацию (HTTP 401).";
        case 403: return L"Сервер запретил доступ (HTTP 403).";
        case 404: return L"Файл не найден на сервере (HTTP 404).";
        case 408: return L"Сервер не ответил вовремя (HTTP 408).";
        case 416: return L"Сервер не смог продолжить загрузку (HTTP 416).";
        case 429: return L"Слишком много запросов, попробуйте позже (HTTP 429).";
        case 500: return L"Ошибка на сервере (HTTP 500).";
        case 502: return L"Сервер-посредник вернул ошибку (HTTP 502).";
        case 503: return L"Сервер временно недоступен (HTTP 503).";
        case 504: return L"Сервер не ответил вовремя (HTTP 504).";
        default: return L"Сервер ответил HTTP " + std::to_wstring(status) + L".";
    }
}

std::wstring DescribeHresult(long hr) {
    switch (static_cast<unsigned long>(hr)) {
        case 0x887A0005ul: return L"Видеокарта сбросила устройство (DXGI_ERROR_DEVICE_REMOVED).";
        case 0x887A0006ul: return L"Видеоустройство зависло (DXGI_ERROR_DEVICE_HUNG).";
        case 0x8899000Cul: return L"Direct2D просит пересоздать устройство.";
        case 0x80070005ul: return L"Отказано в доступе.";
        default: {
            wchar_t text[32];
            swprintf(text, 32, L"HRESULT 0x%08lX.", static_cast<unsigned long>(hr));
            return text;
        }
    }
}

std::wstring ShortenPath(const std::wstring& path, std::size_t maxLength) {
    if (path.size() <= maxLength) return path;
    const std::size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return path;
    const std::wstring tail = path.substr(slash + 1);
    if (tail.size() + 4 >= maxLength) return L"…\\" + tail.substr(0, maxLength - 4);
    std::size_t cut = slash;
    const std::size_t previous = path.find_last_of(L"\\/", cut == 0 ? 0 : cut - 1);
    if (previous != std::wstring::npos) cut = previous;
    return L"…" + path.substr(cut);
}

}  // namespace blueward
