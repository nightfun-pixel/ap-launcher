#include "check.h"
#include "loopback.h"

#include <atomic>
#include <string>
#include <vector>

#include "core/http.h"

void RunHttpTests() {
    {
        const std::string body = "hello, launcher";
        loopback::Server server([&](const loopback::Request&) {
            loopback::Reply reply;
            reply.body = body;
            return reply;
        });
        blueward::WinHttpClient client;
        std::string received;
        const blueward::HttpResult result = client.Get(
            server.Url("/manifest.json"), {{"Accept", "application/json"}},
            [](const blueward::HttpResult&) { return true; },
            [&](const void* data, std::size_t size) {
                received.append(static_cast<const char*>(data), size);
                return true;
            });
        CHECK(result.completed);
        CHECK(result.status == 200);
        CHECK(result.error.empty());
        CHECK(received == body);
        CHECK(result.bodyBytes == body.size());
        CHECK(server.Count() == 1);
        CHECK(server.Requests()[0].Header("accept") == "application/json");
        CHECK(server.Requests()[0].Header("user-agent").find("BLUEWARD/0.1.0") == 0);
        CHECK(server.Requests()[0].Header("user-agent").find("Windows NT 10.0") != std::string::npos);
        CHECK(server.Requests()[0].Header("accept-encoding") == "identity");
    }
    {
        std::string why;
        CHECK(!blueward::IsAllowedUrl("http://example.com/", why));
        CHECK(!why.empty());
        CHECK(blueward::IsAllowedUrl("http://127.0.0.1:9/x", why));
        CHECK(blueward::IsAllowedUrl("https://github.com/nightfun-pixel/ap-launcher/releases/latest/download/manifest.json", why));
        const std::string credentials = "https://user:pass@github.com/nightfun-pixel/ap-launcher/releases/latest/download/manifest.json";
        CHECK(!blueward::IsAllowedUrl(credentials, why));
    }
    {
        const std::string target = "final";
        loopback::Server server([&](const loopback::Request& request) {
            loopback::Reply reply;
            if (request.Path() == "/start") {
                reply.status = 302;
                reply.headers.push_back({"Location", "/final"});
                return reply;
            }
            reply.body = target;
            return reply;
        });
        blueward::WinHttpClient client;
        std::string received;
        const blueward::HttpResult result = client.Get(
            server.Url("/start"), {}, [](const blueward::HttpResult&) { return true; },
            [&](const void* data, std::size_t size) {
                received.append(static_cast<const char*>(data), size);
                return true;
            });
        CHECK(result.completed);
        CHECK(result.status == 200);
        CHECK(received == target);
        CHECK(server.Count() == 2);
    }
    {
        loopback::Server server([](const loopback::Request&) {
            loopback::Reply reply;
            reply.status = 302;
            reply.headers.push_back({"Location", "https://evil.example/payload"});
            return reply;
        });
        blueward::WinHttpClient client;
        const blueward::HttpResult result = client.Get(server.Url("/start"), {}, [](const blueward::HttpResult&) { return true; },
                                                       [](const void*, std::size_t) { return true; });
        CHECK(!result.completed);
        CHECK(!result.error.empty());
        CHECK(server.Count() == 1);
    }
    {
        loopback::Server server([](const loopback::Request&) {
            loopback::Reply reply;
            reply.status = 404;
            reply.body = "not found";
            return reply;
        });
        blueward::WinHttpClient client;
        const blueward::HttpResult result = client.Get(server.Url("/x"), {}, [](const blueward::HttpResult&) { return true; },
                                                       [](const void*, std::size_t) { return true; });
        CHECK(result.completed);
        CHECK(result.status == 404);
    }
    {
        loopback::Server server([](const loopback::Request&) {
            loopback::Reply reply;
            reply.body = std::string(200000, 'x');
            reply.hangUpAfter = 100;
            return reply;
        });
        blueward::WinHttpClient client;
        const blueward::HttpResult result = client.Get(server.Url("/short"), {}, [](const blueward::HttpResult&) { return true; },
                                                       [](const void*, std::size_t) { return true; });
        CHECK(!result.completed);
        CHECK(!result.error.empty());
    }
}
