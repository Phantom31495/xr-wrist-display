#include "net.h"
#include "log.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstring>

namespace xrwrist {
namespace {

bool SetNonBlocking(int fd, bool nb) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    flags = nb ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    return fcntl(fd, F_SETFL, flags) == 0;
}

bool ConnectWithTimeout(int fd, const sockaddr_in& addr, int timeoutMs) {
    if (!SetNonBlocking(fd, true)) return false;
    int rc = connect(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    if (rc == 0) {
        SetNonBlocking(fd, false);
        return true;
    }
    if (errno != EINPROGRESS) return false;
    pollfd pfd{fd, POLLOUT, 0};
    int pr = poll(&pfd, 1, timeoutMs);
    if (pr <= 0) return false;
    int err = 0;
    socklen_t len = sizeof(err);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) != 0) return false;
    if (err != 0) return false;
    SetNonBlocking(fd, false);
    return true;
}

sockaddr_in MakeAddr(const std::string& host, int port) {
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, host.c_str(), &a.sin_addr);
    return a;
}

// Very small JSON string extractor for {"ip":"..."} — avoids a JSON lib.
std::string ExtractJsonString(const std::string& json, const std::string& key) {
    std::string pat = "\"" + key + "\"";
    auto pos = json.find(pat);
    if (pos == std::string::npos) return {};
    pos = json.find(':', pos + pat.size());
    if (pos == std::string::npos) return {};
    pos = json.find('"', pos);
    if (pos == std::string::npos) return {};
    auto end = json.find('"', pos + 1);
    if (end == std::string::npos) return {};
    return json.substr(pos + 1, end - pos - 1);
}

}  // namespace

std::string DiscoverPhone(int timeoutMs) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        LOGW("discovery: socket failed");
        return kFallbackPhoneIp;
    }
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));
    // Bind an ephemeral port so we can receive the reply.
    sockaddr_in bindAddr{};
    bindAddr.sin_family = AF_INET;
    bindAddr.sin_addr.s_addr = INADDR_ANY;
    bindAddr.sin_port = 0;
    if (bind(fd, reinterpret_cast<sockaddr*>(&bindAddr), sizeof(bindAddr)) != 0) {
        LOGW("discovery: bind failed");
        close(fd);
        return kFallbackPhoneIp;
    }
    if (!SetNonBlocking(fd, true)) {
        close(fd);
        return kFallbackPhoneIp;
    }

    sockaddr_in dest = MakeAddr(kBroadcastAddr, kDiscoveryPort);
    const char* probe = "{\"discover\":\"xr-wrist\",\"v\":1}";
    sendto(fd, probe, strlen(probe), 0, reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
    LOGI("discovery: probe sent to %s:%d", kBroadcastAddr, kDiscoveryPort);

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    char buf[512];
    while (std::chrono::steady_clock::now() < deadline) {
        pollfd pfd{fd, POLLIN, 0};
        int pr = poll(&pfd, 1, 100);
        if (pr > 0 && (pfd.revents & POLLIN)) {
            ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
            if (n > 0) {
                buf[n] = '\0';
                std::string ip = ExtractJsonString(buf, "ip");
                if (!ip.empty()) {
                    LOGI("discovery: phone at %s", ip.c_str());
                    close(fd);
                    return ip;
                }
            }
        }
    }
    LOGW("discovery: timeout, using fallback %s", kFallbackPhoneIp);
    close(fd);
    return kFallbackPhoneIp;
}

bool TcpClient::Connect(const std::string& host, int port, int timeoutMs) {
    Close();
    fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) return false;
    sockaddr_in addr = MakeAddr(host, port);
    if (!ConnectWithTimeout(fd_, addr, timeoutMs)) {
        Close();
        return false;
    }
    return true;
}

void TcpClient::Close() {
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
}

void TcpClient::ShutdownRecv() {
    if (fd_ >= 0) {
        shutdown(fd_, SHUT_RDWR);
    }
}

bool TcpClient::SendAll(const void* data, size_t len) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd_, p + sent, len - sent, MSG_NOSIGNAL);
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

bool TcpClient::SendLine(const std::string& line) {
    std::string s = line + "\n";
    return SendAll(s.data(), s.size());
}

bool TcpClient::RecvLine(std::string& out, int timeoutMs) {
    out.clear();
    char c;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (true) {
        auto now = std::chrono::steady_clock::now();
        if (now >= deadline) return false;
        int remain = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
        pollfd pfd{fd_, POLLIN, 0};
        int pr = poll(&pfd, 1, remain);
        if (pr <= 0) return false;
        ssize_t n = recv(fd_, &c, 1, 0);
        if (n <= 0) return false;
        if (c == '\n') return true;
        if (c != '\r') out.push_back(c);
        if (out.size() > 8192) return false;  // sanity cap
    }
}

bool TcpClient::RecvExact(void* data, size_t len, int timeoutMs) {
    uint8_t* p = static_cast<uint8_t*>(data);
    size_t got = 0;
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeoutMs);
    while (got < len) {
        auto now = std::chrono::steady_clock::now();
        if (now >= deadline) return false;
        int remain = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now)
                .count());
        pollfd pfd{fd_, POLLIN, 0};
        int pr = poll(&pfd, 1, remain);
        if (pr <= 0) return false;  // timeout or error
        ssize_t n = recv(fd_, p + got, len - got, 0);
        if (n <= 0) return false;
        got += static_cast<size_t>(n);
    }
    return true;
}

bool ControlClient::Start(const std::string& host) {
    if (running_) return true;
    running_ = true;
    thread_ = std::thread(&ControlClient::ThreadLoop, this, host);
    return true;
}

void ControlClient::Stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
    std::lock_guard<std::mutex> lock(mutex_);
    while (!queue_.empty()) queue_.pop();
}

void ControlClient::SendTouch(const std::string& action, float x, float y) {
    char buf[128];
    snprintf(buf, sizeof(buf), "{\"t\":\"touch\",\"a\":\"%s\",\"x\":%.4f,\"y\":%.4f}",
             action.c_str(), x, y);
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.size() < 64) queue_.push(buf);
}

void ControlClient::SendCmd(const std::string& cmd) {
    std::string s = "{\"t\":\"cmd\",\"c\":\"" + cmd + "\"}";
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.size() < 64) queue_.push(s);
}

void ControlClient::ThreadLoop(const std::string host) {
    TcpClient tcp;
    if (!tcp.Connect(host, kControlPort, 5000)) {
        LOGW("control: connect to %s:%d failed", host.c_str(), kControlPort);
        running_ = false;
        return;
    }
    LOGI("control: connected to %s:%d", host.c_str(), kControlPort);
    while (running_) {
        std::string msg;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!queue_.empty()) {
                msg = queue_.front();
                queue_.pop();
            }
        }
        if (!msg.empty()) {
            if (!tcp.SendLine(msg)) {
                LOGW("control: send failed, stopping");
                break;
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    tcp.Close();
    running_ = false;
}

}  // namespace xrwrist
