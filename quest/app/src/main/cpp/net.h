#pragma once
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <queue>
#include <atomic>
#include <cstdint>

namespace xrwrist {

// ---- tunables (must match phone track) ----
constexpr int kDiscoveryPort = 8899;
constexpr int kVideoPort = 8900;
constexpr int kControlPort = 8901;
// Limited broadcast: works on any subnet (VRC: no hardcoded LAN assumptions).
constexpr const char* kBroadcastAddr = "255.255.255.255";
// Last-resort fallback if discovery finds nothing (user's phone on his LAN).
constexpr const char* kFallbackPhoneIp = "192.168.1.172";

// UDP discovery: broadcast and wait for {"ip":"...","v":1,"name":"ziggy"}.
// Returns phone IP, or kFallbackPhoneIp on timeout/failure.
std::string DiscoverPhone(int timeoutMs = 3000);

// Minimal blocking TCP client.
class TcpClient {
public:
    TcpClient() = default;
    ~TcpClient() { Close(); }
    TcpClient(const TcpClient&) = delete;
    TcpClient& operator=(const TcpClient&) = delete;

    bool Connect(const std::string& host, int port, int timeoutMs = 5000);
    void Close();
    bool IsOpen() const { return fd_ >= 0; }

    bool SendAll(const void* data, size_t len);
    bool SendLine(const std::string& line);  // appends '\n'
    // Reads until '\n' (excluded). Returns false on error/EOF.
    bool RecvLine(std::string& out, int timeoutMs = 10000);
    // Reads exactly len bytes. Returns false on error/EOF/timeout.
    // VRC robustness: bounded wait so a dead peer can't wedge the thread.
    bool RecvExact(void* data, size_t len, int timeoutMs = 15000);
    // Interrupts any blocking recv (for shutdown from another thread).
    void ShutdownRecv();

private:
    int fd_ = -1;
};

// Background control channel: TCP phone:8901, newline-delimited JSON.
// Thread-safe; reconnects are NOT automatic (call Start again).
class ControlClient {
public:
    ControlClient() = default;
    ~ControlClient() { Stop(); }

    bool Start(const std::string& host);
    void Stop();
    bool IsRunning() const { return running_; }

    void SendTouch(const std::string& action, float x, float y);  // action: down|move|up
    void SendCmd(const std::string& cmd);                        // screen_off|screen_on

private:
    void ThreadLoop(const std::string host);

    std::atomic<bool> running_{false};
    std::thread thread_;
    std::mutex mutex_;
    std::queue<std::string> queue_;
};

}  // namespace xrwrist
