#pragma once

// Shared scaffolding for the tests that talk to a live WebServer.

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>
#include <ixwebsocket/IXWebSocket.h>

namespace webtest {

inline int free_port() {
    int s = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = 0;
    ::bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a));
    socklen_t len = sizeof(a);
    ::getsockname(s, reinterpret_cast<sockaddr*>(&a), &len);
    ::close(s);
    return ntohs(a.sin_port);
}

struct TestClient {
    ix::WebSocket ws;
    std::recursive_mutex m;
    std::condition_variable_any cv;
    std::vector<std::string> texts;
    std::vector<std::string> bins;
    bool open = false, closed = false;
    uint16_t close_code = 0;

    explicit TestClient(const std::string& url, const std::string& origin = "") {
        ws.setUrl(url);
        ws.disableAutomaticReconnection();
        ws.disablePerMessageDeflate();
        if (!origin.empty()) { ix::WebSocketHttpHeaders h; h["Origin"] = origin; ws.setExtraHeaders(h); }
        ws.setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
            std::lock_guard<std::recursive_mutex> lock(m);
            if (msg->type == ix::WebSocketMessageType::Open) open = true;
            else if (msg->type == ix::WebSocketMessageType::Close) { closed = true; close_code = msg->closeInfo.code; }
            else if (msg->type == ix::WebSocketMessageType::Error) closed = true;
            else if (msg->type == ix::WebSocketMessageType::Message) (msg->binary ? bins : texts).push_back(msg->str);
            cv.notify_all();
        });
        ws.start();
    }
    ~TestClient() { ws.stop(); }
    template <typename F> bool wait(F pred, int ms = 3000) {
        std::unique_lock<std::recursive_mutex> lock(m);
        return cv.wait_for(lock, std::chrono::milliseconds(ms), pred);
    }
    std::string text_with(const char* t) {
        std::lock_guard<std::recursive_mutex> lock(m);
        for (size_t i = texts.size(); i-- > 0;) if (texts[i].find(std::string("\"t\":\"") + t + "\"") != std::string::npos) return texts[i];
        return "";
    }
};

}
