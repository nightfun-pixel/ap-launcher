#pragma once
// A small HTTP server on the loopback interface. It serves one connection at a time, records every
// request and lets a test answer each request by hand. Nothing leaves the machine.

#include <winsock2.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace loopback {

struct Request {
    std::string method, target;
    std::map<std::string, std::string> headers;
    std::string body;
    std::string Path() const { return target.substr(0, target.find('?')); }
    std::string Header(const std::string& name) const {
        const auto it = headers.find(name);
        return it == headers.end() ? std::string() : it->second;
    }
};

struct Reply {
    int status = 200;
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string contentType = "application/json";
    int hangUpAfter = -1;
};

class Server {
public:
    using Handler = std::function<Reply(const Request&)>;

    explicit Server(Handler handler) : handler_(std::move(handler)) {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
        listener_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof address);
        listen(listener_, 8);
        int length = sizeof address;
        getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &length);
        port_ = ntohs(address.sin_port);
        thread_ = std::thread([this] { Loop(); });
    }
    ~Server() {
        stop_ = true;
        thread_.join();
        closesocket(listener_);
        WSACleanup();
    }
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    std::string Url(const std::string& path = std::string()) const {
        return "http://127.0.0.1:" + std::to_string(port_) + path;
    }

    std::vector<Request> Requests() const {
        std::lock_guard<std::mutex> guard(lock_);
        return seen_;
    }
    int Count() const { return static_cast<int>(Requests().size()); }
    void Forget() {
        std::lock_guard<std::mutex> guard(lock_);
        seen_.clear();
    }

private:
    static const char* Reason(int status) {
        switch (status) {
            case 200: return "OK";
            case 206: return "Partial Content";
            case 302: return "Found";
            case 400: return "Bad Request";
            case 403: return "Forbidden";
            case 404: return "Not Found";
            case 416: return "Range Not Satisfiable";
            case 503: return "Service Unavailable";
            default: return "Status";
        }
    }

    static void SendAll(SOCKET s, const char* data, std::size_t size) {
        while (size > 0) {
            const int sent = send(s, data, static_cast<int>(size), 0);
            if (sent <= 0) return;
            data += sent;
            size -= static_cast<std::size_t>(sent);
        }
    }

    void Loop() {
        while (!stop_) {
            fd_set set;
            FD_ZERO(&set);
            FD_SET(listener_, &set);
            timeval wait{0, 100000};
            if (select(0, &set, nullptr, nullptr, &wait) <= 0) continue;
            SOCKET client = accept(listener_, nullptr, nullptr);
            if (client == INVALID_SOCKET) continue;
            Serve(client);
            closesocket(client);
        }
    }

    void Serve(SOCKET client) {
        std::string raw;
        char buffer[4096];
        while (raw.find("\r\n\r\n") == std::string::npos && raw.size() < (1u << 20)) {
            const int n = recv(client, buffer, sizeof buffer, 0);
            if (n <= 0) return;
            raw.append(buffer, static_cast<std::size_t>(n));
        }
        const std::size_t head = raw.find("\r\n\r\n");
        Request request;
        {
            const std::size_t lineEnd = raw.find("\r\n");
            const std::string first = raw.substr(0, lineEnd);
            const std::size_t sp1 = first.find(' '), sp2 = first.find(' ', sp1 + 1);
            request.method = first.substr(0, sp1);
            request.target = first.substr(sp1 + 1, sp2 - sp1 - 1);
            std::size_t at = lineEnd + 2;
            while (at < head) {
                const std::size_t end = raw.find("\r\n", at);
                const std::string line = raw.substr(at, end - at);
                const std::size_t colon = line.find(':');
                if (colon != std::string::npos) {
                    std::string name = line.substr(0, colon);
                    for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    std::size_t value = colon + 1;
                    while (value < line.size() && line[value] == ' ') ++value;
                    request.headers[name] = line.substr(value);
                }
                at = end + 2;
            }
        }
        const std::size_t want = static_cast<std::size_t>(std::strtoull(request.Header("content-length").c_str(), nullptr, 10));
        std::string body = raw.substr(head + 4);
        while (body.size() < want) {
            const int n = recv(client, buffer, sizeof buffer, 0);
            if (n <= 0) break;
            body.append(buffer, static_cast<std::size_t>(n));
        }
        request.body = body.substr(0, want);
        {
            std::lock_guard<std::mutex> guard(lock_);
            seen_.push_back(request);
        }

        const Reply reply = handler_(request);
        std::string text = std::string("HTTP/1.1 ") + std::to_string(reply.status) + " " + Reason(reply.status) + "\r\n";
        text += "Content-Type: " + reply.contentType + "\r\n";
        text += "Content-Length: " + std::to_string(reply.body.size()) + "\r\n";
        for (const auto& h : reply.headers) text += h.first + ": " + h.second + "\r\n";
        text += "Connection: close\r\n\r\n";
        SendAll(client, text.data(), text.size());
        const std::size_t send = reply.hangUpAfter >= 0
                                     ? std::min<std::size_t>(reply.body.size(), static_cast<std::size_t>(reply.hangUpAfter))
                                     : reply.body.size();
        SendAll(client, reply.body.data(), send);
    }

    Handler handler_;
    SOCKET listener_ = INVALID_SOCKET;
    int port_ = 0;
    std::atomic<bool> stop_{false};
    std::thread thread_;
    mutable std::mutex lock_;
    std::vector<Request> seen_;
};

inline Reply ServeBytes(const Request& request, const std::string& data) {
    Reply reply;
    reply.contentType = "application/octet-stream";
    const std::string range = request.Header("range");
    if (range.rfind("bytes=", 0) == 0) {
        const std::size_t start = static_cast<std::size_t>(std::strtoull(range.c_str() + 6, nullptr, 10));
        if (start >= data.size()) {
            reply.status = 416;
            return reply;
        }
        reply.status = 206;
        reply.body = data.substr(start);
        reply.headers.push_back({"Content-Range", "bytes " + std::to_string(start) + "-" + std::to_string(data.size() - 1) + "/" +
                                 std::to_string(data.size())});
        return reply;
    }
    reply.body = data;
    return reply;
}

}  // namespace loopback
