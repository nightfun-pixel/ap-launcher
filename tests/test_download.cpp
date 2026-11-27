#include "check.h"
#include "helpers.h"
#include "loopback.h"

#include <atomic>
#include <string>

#include "core/download.h"
#include "core/http.h"
#include "core/sha256.h"

namespace {

std::string Blob(std::size_t size, char seed) {
    std::string data(size, '\0');
    for (std::size_t i = 0; i < size; ++i) data[i] = static_cast<char>((i * 17 + static_cast<unsigned char>(seed)) & 0xFF);
    return data;
}

std::string HashOf(const std::string& data) {
    std::string hex;
    blueward::Sha256String(data, hex);
    return hex;
}

}  // namespace

void RunDownloadTests() {
    {
        const std::string data = Blob(300000, 3);
        loopback::Server server([&](const loopback::Request& request) { return loopback::ServeBytes(request, data); });
        const std::wstring part = testutil::Path(L"download-resume.part");
        CHECK(testutil::Write(part, data.substr(0, 12345)));
        blueward::WinHttpClient client;
        std::atomic<bool> cancel{false};
        std::uint64_t lastDone = 0, lastTotal = 0;
        const blueward::DownloadResult result = blueward::DownloadToPart(
            client, server.Url("/payload.bin"), part, data.size(), HashOf(data),
            [&](const char*, std::uint64_t done, std::uint64_t total) {
                lastDone = done;
                lastTotal = total;
                return true;
            },
            cancel);
        CHECK_MSG(result.ok, result.error);
        CHECK(result.httpStatus == 206);
        CHECK(testutil::Read(part) == data);
        CHECK(lastDone == data.size());
        CHECK(lastTotal == data.size());
        const std::vector<loopback::Request> requests = server.Requests();
        CHECK(!requests.empty());
        CHECK(requests[0].Header("range") == "bytes=12345-");
    }
    {
        const std::string data = Blob(120000, 5);
        loopback::Server server([&](const loopback::Request&) {
            loopback::Reply reply;
            reply.body = data;
            return reply;
        });
        const std::wstring part = testutil::Path(L"download-ignore.part");
        CHECK(testutil::Write(part, data.substr(0, 7777)));
        blueward::WinHttpClient client;
        std::atomic<bool> cancel{false};
        const blueward::DownloadResult result =
            blueward::DownloadToPart(client, server.Url("/payload.bin"), part, data.size(), HashOf(data), {}, cancel);
        CHECK_MSG(result.ok, result.error);
        CHECK(result.httpStatus == 200);
        CHECK(testutil::Read(part) == data);
    }
    {
        const std::string data = Blob(80000, 7);
        loopback::Server server([&](const loopback::Request& request) { return loopback::ServeBytes(request, data); });
        const std::wstring part = testutil::Path(L"download-badhash.part");
        blueward::WinHttpClient client;
        std::atomic<bool> cancel{false};
        const blueward::DownloadResult result =
            blueward::DownloadToPart(client, server.Url("/payload.bin"), part, data.size(), std::string(64, '0'), {}, cancel);
        CHECK(!result.ok);
        CHECK(!result.error.empty());
        CHECK(!testutil::Exists(part));
    }
    {
        const std::string data = Blob(400000, 9);
        loopback::Server server([&](const loopback::Request& request) { return loopback::ServeBytes(request, data); });
        const std::wstring part = testutil::Path(L"download-cancel.part");
        blueward::WinHttpClient client;
        std::atomic<bool> cancel{false};
        const blueward::DownloadResult result = blueward::DownloadToPart(
            client, server.Url("/payload.bin"), part, data.size(), HashOf(data),
            [&](const char*, std::uint64_t done, std::uint64_t) {
                if (done > 20000) {
                    cancel = true;
                    return false;
                }
                return true;
            },
            cancel);
        CHECK(!result.ok);
        CHECK(result.cancelled);
        CHECK(result.resumable);
        CHECK(testutil::Exists(part));
        const std::uint64_t have = testutil::Size(part);
        CHECK(have > 0);
        CHECK(have < data.size());
    }
    {
        loopback::Server server([](const loopback::Request&) {
            loopback::Reply reply;
            reply.status = 404;
            return reply;
        });
        const std::wstring part = testutil::Path(L"download-404.part");
        blueward::WinHttpClient client;
        std::atomic<bool> cancel{false};
        const blueward::DownloadResult result =
            blueward::DownloadToPart(client, server.Url("/payload.bin"), part, 100, std::string(64, '0'), {}, cancel);
        CHECK(!result.ok);
        CHECK(result.httpStatus == 404);
        CHECK(!result.error.empty());
    }
    {
        loopback::Server server([](const loopback::Request&) {
            loopback::Reply reply;
            reply.body = "{\"ok\":true}";
            return reply;
        });
        blueward::WinHttpClient client;
        std::string body;
        std::string error;
        CHECK(blueward::FetchText(client, server.Url("/m.json"), 1024, body, error));
        CHECK(body == "{\"ok\":true}");
        CHECK(error.empty());
    }
}
