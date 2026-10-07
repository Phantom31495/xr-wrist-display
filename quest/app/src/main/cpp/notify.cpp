#include "notify.h"
#include "log.h"

#include <chrono>

namespace xrwrist {

Notify& Notify::Instance() {
    static Notify instance;
    return instance;
}

static double NowSec() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

void Notify::Push(NotifyLevel level, const std::string& title,
                  const std::string& action) {
    double now = NowSec();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!startSet_) {
            startTime_ = now;
            startSet_ = true;
        }
        messages_.push_back({level, title, action, now - startTime_});
        if (messages_.size() > kMaxMessages)
            messages_.erase(messages_.begin());
    }
    // Also log — notifications are user-facing, logs are developer-facing.
    const char* lvl = level == NotifyLevel::Info ? "INFO"
        : level == NotifyLevel::Warn ? "WARN" : "ERROR";
    if (action.empty())
        LOGI("notify [%s]: %s", lvl, title.c_str());
    else
        LOGI("notify [%s]: %s — %s", lvl, title.c_str(), action.c_str());
}

void Notify::Info(const std::string& title, const std::string& action) {
    Push(NotifyLevel::Info, title, action);
}
void Notify::Warn(const std::string& title, const std::string& action) {
    Push(NotifyLevel::Warn, title, action);
}
void Notify::Error(const std::string& title, const std::string& action) {
    Push(NotifyLevel::Error, title, action);
}

std::vector<Notification> Notify::Recent(double nowSec, double maxAgeSec) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Notification> out;
    for (auto it = messages_.rbegin(); it != messages_.rend(); ++it) {
        if (nowSec - it->timestamp <= maxAgeSec)
            out.push_back(*it);
        else
            break;
    }
    return out;
}

bool Notify::Latest(Notification& out, double nowSec, double maxAgeSec) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = messages_.rbegin(); it != messages_.rend(); ++it) {
        if (nowSec - it->timestamp <= maxAgeSec) {
            out = *it;
            return true;
        }
        break;
    }
    return false;
}

void Notify::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    messages_.clear();
}

}  // namespace xrwrist
