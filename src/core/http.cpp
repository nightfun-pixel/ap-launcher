#include "core/http.h"

#include <windows.h>
#include <winhttp.h>

#include <cstdlib>
#include <cstring>
#include <set>
#include <vector>

#include "core/log.h"
#include "core/manifest.h"
#include "core/text.h"
#include "core/version.h"

namespace blueward {

namespace {

class Handle {
public:
    explicit Handle(HINTERNET handle = nullptr) : handle_(handle) {}
    ~Handle() {
        if (handle_) WinHttpCloseHandle(handle_);
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HINTERNET get() const { return handle_; }
    explicit operator bool() const { return handle_ != nullptr; }

private:
    HINTERNET handle_;
};

struct Cracked {
    bool secure = false;
    bool loopback = false;
    bool credentials = false;
    INTERNET_PORT port = 0;
    std::wstring host, path;
};

bool Crack(const std::string& url, Cracked& out) {
    const std::wstring wide = Widen(url);
    if (wide.empty()) return false;
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof parts;
    parts.dwSchemeLength = parts.dwHostNameLength = parts.dwUserNameLength = parts.dwPasswordLength = parts.dwUrlPathLength =
        parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wide.c_str(), static_cast<DWORD>(wide.size()), 0, &parts)) return false;
    if (parts.nScheme != INTERNET_SCHEME_HTTPS && parts.nScheme != INTERNET_SCHEME_HTTP) return false;
    out.secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
    out.port = parts.nPort;
    out.host.assign(parts.lpszHostName, parts.dwHostNameLength);
    out.path.assign(parts.lpszUrlPath, parts.dwUrlPathLength);
    out.path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    if (out.path.empty()) out.path = L"/";
    out.credentials = parts.dwUserNameLength != 0 || parts.dwPasswordLength != 0;
    out.loopback = out.host == L"localhost" || out.host == L"127.0.0.1" || out.host == L"[::1]" || out.host == L"::1";
    return !out.host.empty();
}

std::string QueryHeader(HINTERNET request, const wchar_t* name) {
    DWORD size = 0;
    WinHttpQueryHeaders(request, WINHTTP_QUERY_CUSTOM, name, WINHTTP_NO_OUTPUT_BUFFER, &size, WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size == 0) return {};
    std::wstring value(size / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_CUSTOM, name, value.data(), &size, WINHTTP_NO_HEADER_INDEX)) return {};
    value.resize(size / sizeof(wchar_t));
    return Narrow(value);
}

void Fail(HttpResult& result, const char* step) {
    result.systemError = GetLastError();
    result.error = std::string("WinHTTP error ") + std::to_string(result.systemError) + " (" + step + ")";
}

bool IsRedirect(int status) { return status == 301 || status == 302 || status == 303 || status == 307 || status == 308; }

// One request without following a redirect. A 3xx answer returns its Location and never touches the
// caller's body, so only the final response is verified or written.
HttpResult Once(const std::string& url, const std::vector<HttpHeader>& headers, const HeadersHandler& onHeaders, const BodyHandler& onBody) {
    HttpResult result;
    Cracked cracked;
    if (!Crack(url, cracked)) {
        result.error = "not a valid http or https address";
        return result;
    }
    HINTERNET opened = WinHttpOpen((std::wstring(L"BLUEWARD/") + Widen(kBluewardVersion) + L" (Windows NT 10.0; Win64; x64)").c_str(),
                                   cracked.loopback ? WINHTTP_ACCESS_TYPE_NO_PROXY : WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                   WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!opened)
        opened = WinHttpOpen((std::wstring(L"BLUEWARD/") + Widen(kBluewardVersion) + L" (Windows NT 10.0; Win64; x64)").c_str(),
                             WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    Handle session(opened);
    if (!session) {
        Fail(result, "open session");
        return result;
    }
    WinHttpSetTimeouts(session.get(), 15000, 15000, 60000, 60000);
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | 0x2000;  // 0x2000 is TLS 1.3 on newer systems
    if (!WinHttpSetOption(session.get(), WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols)) {
        protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
        WinHttpSetOption(session.get(), WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols);
    }
    DWORD disable = WINHTTP_DISABLE_COOKIES;
    WinHttpSetOption(session.get(), WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof disable);

    Handle connection(WinHttpConnect(session.get(), cracked.host.c_str(), cracked.port, 0));
    if (!connection) {
        Fail(result, "connect");
        return result;
    }
    Handle request(WinHttpOpenRequest(connection.get(), L"GET", cracked.path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                      cracked.secure ? WINHTTP_FLAG_SECURE : 0));
    if (!request) {
        Fail(result, "open request");
        return result;
    }
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    if (!WinHttpSetOption(request.get(), WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof policy)) {
        Fail(result, "refuse redirects");
        return result;
    }
    for (const HttpHeader& header : headers) {
        const std::wstring line = Widen(header.name + ": " + header.value);
        WinHttpAddRequestHeaders(request.get(), line.c_str(), static_cast<DWORD>(-1), WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    }
    WinHttpAddRequestHeaders(request.get(), L"Accept-Encoding: identity", static_cast<DWORD>(-1),
                             WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    if (!WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.get(), nullptr)) {
        Fail(result, "send request");
        return result;
    }
    DWORD status = 0, size = sizeof status;
    if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                             WINHTTP_NO_HEADER_INDEX)) {
        Fail(result, "read status");
        return result;
    }
    result.status = static_cast<int>(status);
    const std::string length = QueryHeader(request.get(), L"Content-Length");
    if (!length.empty()) {
        char* end = nullptr;
        const unsigned long long value = std::strtoull(length.c_str(), &end, 10);
        if (end != length.c_str() && *end == '\0') {
            result.hasContentLength = true;
            result.contentLength = value;
        }
    }
    result.contentRange = QueryHeader(request.get(), L"Content-Range");
    result.location = QueryHeader(request.get(), L"Location");
    const std::string encoding = QueryHeader(request.get(), L"Content-Encoding");
    if (!encoding.empty() && _stricmp(encoding.c_str(), "identity") != 0) {
        result.error = "the server used an unsupported content encoding: " + encoding;
        return result;
    }
    const std::string retryAfter = QueryHeader(request.get(), L"Retry-After");
    if (!retryAfter.empty()) {
        char* end = nullptr;
        const long seconds = std::strtol(retryAfter.c_str(), &end, 10);
        if (end != retryAfter.c_str() && *end == '\0' && seconds >= 0) result.retryAfterSeconds = static_cast<int>(std::min<long>(seconds, 60));
    }
    if (IsRedirect(result.status)) {
        result.completed = true;
        return result;
    }
    if (onHeaders && !onHeaders(result)) {
        result.error = "stopped by the caller";
        return result;
    }
    std::vector<char> buffer(64 * 1024);
    std::uint64_t received = 0;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.get(), &available)) {
            Fail(result, "read body");
            return result;
        }
        if (available == 0) break;
        DWORD got = 0;
        const DWORD want = available < buffer.size() ? available : static_cast<DWORD>(buffer.size());
        if (!WinHttpReadData(request.get(), buffer.data(), want, &got)) {
            Fail(result, "read body");
            return result;
        }
        if (got == 0) break;
        received += got;
        result.bodyBytes = received;
        if (onBody && !onBody(buffer.data(), got)) {
            result.error = "stopped by the caller";
            return result;
        }
    }
    if (result.hasContentLength && received < result.contentLength) {
        result.error = "the connection closed before the whole body arrived";
        return result;
    }
    result.completed = true;
    return result;
}

}  // namespace

bool IsAllowedUrl(const std::string& url, std::string& why) {
    std::string loopbackWhy;
    if (IsLoopbackUrl(url, loopbackWhy)) return true;
    return IsAllowedDownloadUrl(url, why);
}

HttpResult WinHttpClient::Get(const std::string& url, const std::vector<HttpHeader>& headers, const HeadersHandler& onHeaders,
                              const BodyHandler& onBody) {
    std::string current = url;
    std::set<std::string> visited;
    for (int hop = 0; hop < 6; ++hop) {
        std::string why;
        const bool first = hop == 0;
        if (first ? !IsAllowedUrl(current, why) : !IsAllowedRedirectUrl(current, why)) {
            HttpResult refused;
            refused.error = (first ? "refusing " : "refusing redirect to ") + current + ": " + why;
            return refused;
        }
        if (!visited.insert(current).second) {
            HttpResult loop;
            loop.error = "redirect loop detected at " + current;
            return loop;
        }
        HttpResult result = Once(current, headers, onHeaders, onBody);
        if (!result.completed) return result;
        if (!IsRedirect(result.status)) return result;
        if (result.location.empty()) {
            result.completed = false;
            result.error = "the server answered a redirect without a Location header";
            return result;
        }
        if (hop == 5) {
            result.completed = false;
            result.error = "too many redirects";
            return result;
        }
        current = ResolveUrl(current, result.location);
    }
    HttpResult exhausted;
    exhausted.error = "too many redirects";
    return exhausted;
}

}  // namespace blueward
