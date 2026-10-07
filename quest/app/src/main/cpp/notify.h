#pragma once
// v0.5.0: User-facing notification system.
//
// Every failure mode gets a human-readable message + recovery action.
// No silent failures. No cryptic logs as the only feedback.
//
// Usage:
//   Notify::Info("Connected to phone");
//   Notify::Warn("Connection lost", "Reconnecting...");
//   Notify::Error("Phone not found", "Check WiFi, tap Start on phone");
//
// Messages are queued and displayed as toasts in VR (via TextOverlay).
// The most recent message is also available for the Godmode console.

#include <mutex>
#include <string>
#include <vector>

namespace xrwrist {

enum class NotifyLevel { Info, Warn, Error };

struct Notification {
    NotifyLevel level;
    std::string title;    // short, e.g. "Connection lost"
    std::string action;   // recovery hint, e.g. "Reconnecting..."
    double timestamp;     // seconds since app start (for expiry)
};

class Notify {
public:
    static Notify& Instance();

    void Info(const std::string& title, const std::string& action = "");
    void Warn(const std::string& title, const std::string& action = "");
    void Error(const std::string& title, const std::string& action = "");

    // Returns messages newer than maxAgeSec, newest first.
    std::vector<Notification> Recent(double nowSec, double maxAgeSec = 10.0);

    // The single most recent message (for HUD display).
    bool Latest(Notification& out, double nowSec, double maxAgeSec = 8.0);

    void Clear();

private:
    Notify() = default;
    void Push(NotifyLevel level, const std::string& title,
              const std::string& action);

    mutable std::mutex mutex_;
    std::vector<Notification> messages_;
    static constexpr size_t kMaxMessages = 32;
    double startTime_ = 0.0;
    bool startSet_ = false;
};

}  // namespace xrwrist
