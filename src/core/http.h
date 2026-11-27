#pragma once
// The launcher's only network code: WinHTTP GET with TLS 1.2+, cookies off, manual redirects that
// may only land on github.com, objects.githubusercontent.com or release-assets.githubusercontent.com,
// and no identifying headers beyond the user agent. Plain http is accepted only for a loopback
// address, which the tests use.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace blueward {

struct HttpHeader {
    std::string name, value;
};

struct HttpResult {
    bool completed = false;  // the whole body arrived (an error status still counts as completed)
    int status = 0;
    bool hasContentLength = false;
    std::uint64_t contentLength = 0;
    std::string contentRange;  // "bytes 1000-1999/5000"
    std::string location;
    int retryAfterSeconds = 0;
    std::string error;
    unsigned long systemError = 0;
    std::uint64_t bodyBytes = 0;
};

using HeadersHandler = std::function<bool(const HttpResult& headers)>;
using BodyHandler = std::function<bool(const void* data, std::size_t size)>;

bool IsAllowedUrl(const std::string& url, std::string& why);
bool IsLoopbackUrl(const std::string& url, std::string& why);

class HttpClient {
public:
    virtual ~HttpClient() = default;
    virtual HttpResult Get(const std::string& url, const std::vector<HttpHeader>& headers, const HeadersHandler& onHeaders,
                           const BodyHandler& onBody) = 0;
};

class WinHttpClient final : public HttpClient {
public:
    HttpResult Get(const std::string& url, const std::vector<HttpHeader>& headers, const HeadersHandler& onHeaders,
                   const BodyHandler& onBody) override;
};

}  // namespace blueward
