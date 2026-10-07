#include "config.h"
#include "log.h"

#include <cstdio>
#include <cstring>

namespace xrwrist {

Config& Config::Instance() {
    static Config instance;
    return instance;
}

void Config::ResetDefaults() {
    std::lock_guard<std::mutex> lock(mutex_);
    // Network
    ints_[ConfigKey::DiscoveryPort] = 8899;
    ints_[ConfigKey::VideoPort] = 8900;
    ints_[ConfigKey::ControlPort] = 8901;
    ints_[ConfigKey::DiscoveryTimeoutMs] = 3000;
    ints_[ConfigKey::ReconnectIntervalMs] = 5000;
    strings_[ConfigKey::PhoneIpOverride] = "";
    // Display
    floats_[ConfigKey::GlanceSizeM] = 0.08f;
    floats_[ConfigKey::EngagedSizeM] = 0.42f;
    floats_[ConfigKey::GazeDwellSec] = 1.5f;
    floats_[ConfigKey::GazeLostSec] = 2.0f;
    floats_[ConfigKey::WorldDistanceM] = 0.7f;
    floats_[ConfigKey::WorldHeightOffsetM] = -0.12f;
    // Interaction
    bools_[ConfigKey::AutoTransition] = true;
    floats_[ConfigKey::WristRaiseSec] = 4.0f;
    floats_[ConfigKey::TouchDistanceM] = 0.030f;
    floats_[ConfigKey::PinchThreshold] = 0.8f;
    bools_[ConfigKey::VoiceEnabled] = true;
    // Avatar
    bools_[ConfigKey::AvatarEnabled] = true;
    floats_[ConfigKey::WatchGlowIntensity] = 1.0f;
    // Godmode
    bools_[ConfigKey::GodmodeEnabled] = true;
    // v0.6.0: environment + menu
    bools_[ConfigKey::PassthroughEnabled] = true;
    bools_[ConfigKey::EnvironmentEnabled] = true;
    // v0.6.2: phone-screen projection UI
    ints_[ConfigKey::ProjectionMode] = 0;
    floats_[ConfigKey::DisplayBrightness] = 1.0f;
    bools_[ConfigKey::OrientationLocked] = false;
    // System
    bools_[ConfigKey::FirstRunComplete] = false;
}

bool Config::GetBool(ConfigKey key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = bools_.find(key);
    return it != bools_.end() ? it->second : false;
}
int Config::GetInt(ConfigKey key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = ints_.find(key);
    return it != ints_.end() ? it->second : 0;
}
float Config::GetFloat(ConfigKey key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = floats_.find(key);
    return it != floats_.end() ? it->second : 0.0f;
}
std::string Config::GetString(ConfigKey key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = strings_.find(key);
    return it != strings_.end() ? it->second : "";
}

void Config::SetBool(ConfigKey key, bool value) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        bools_[key] = value;
    }
    Save(path_);
    NotifyChanged(key);
}
void Config::SetInt(ConfigKey key, int value) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ints_[key] = value;
    }
    Save(path_);
    NotifyChanged(key);
}
void Config::SetFloat(ConfigKey key, float value) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        floats_[key] = value;
    }
    Save(path_);
    NotifyChanged(key);
}
void Config::SetString(ConfigKey key, const std::string& value) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        strings_[key] = value;
    }
    Save(path_);
    NotifyChanged(key);
}

void Config::AddListener(ChangeCallback cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    listeners_.push_back(cb);
}

void Config::NotifyChanged(ConfigKey key) {
    std::vector<ChangeCallback> cbs;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cbs = listeners_;
    }
    for (auto& cb : cbs) cb(key);
}

static const char* KeyName(ConfigKey key) {
    switch (key) {
        case ConfigKey::DiscoveryPort: return "discovery_port";
        case ConfigKey::VideoPort: return "video_port";
        case ConfigKey::ControlPort: return "control_port";
        case ConfigKey::DiscoveryTimeoutMs: return "discovery_timeout_ms";
        case ConfigKey::ReconnectIntervalMs: return "reconnect_interval_ms";
        case ConfigKey::PhoneIpOverride: return "phone_ip_override";
        case ConfigKey::GlanceSizeM: return "glance_size_m";
        case ConfigKey::EngagedSizeM: return "engaged_size_m";
        case ConfigKey::GazeDwellSec: return "gaze_dwell_sec";
        case ConfigKey::GazeLostSec: return "gaze_lost_sec";
        case ConfigKey::WorldDistanceM: return "world_distance_m";
        case ConfigKey::WorldHeightOffsetM: return "world_height_offset_m";
        case ConfigKey::AutoTransition: return "auto_transition";
        case ConfigKey::WristRaiseSec: return "wrist_raise_sec";
        case ConfigKey::TouchDistanceM: return "touch_distance_m";
        case ConfigKey::PinchThreshold: return "pinch_threshold";
        case ConfigKey::VoiceEnabled: return "voice_enabled";
        case ConfigKey::AvatarEnabled: return "avatar_enabled";
        case ConfigKey::WatchGlowIntensity: return "watch_glow_intensity";
        case ConfigKey::GodmodeEnabled: return "godmode_enabled";
        case ConfigKey::PassthroughEnabled: return "passthrough_enabled";
        case ConfigKey::EnvironmentEnabled: return "environment_enabled";
        case ConfigKey::ProjectionMode: return "projection_mode";
        case ConfigKey::DisplayBrightness: return "display_brightness";
        case ConfigKey::OrientationLocked: return "orientation_locked";
        case ConfigKey::FirstRunComplete: return "first_run_complete";
    }
    return "unknown";
}

bool Config::Load(const std::string& path) {
    if (path.empty()) return false;
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return false;  // first run — defaults stand
    char line[512];
    std::lock_guard<std::mutex> lock(mutex_);
    while (fgets(line, sizeof(line), f)) {
        // Skip comments and blank lines.
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char* key = line;
        char* val = eq + 1;
        // Trim trailing newline from val.
        val[strcspn(val, "\r\n")] = '\0';
        // Match against known keys.
        for (int k = 0; k <= (int)ConfigKey::FirstRunComplete; ++k) {
            ConfigKey ck = (ConfigKey)k;
            if (strcmp(key, KeyName(ck)) == 0) {
                if (bools_.count(ck))
                    bools_[ck] = (strcmp(val, "1") == 0 || strcmp(val, "true") == 0);
                else if (ints_.count(ck))
                    ints_[ck] = atoi(val);
                else if (floats_.count(ck))
                    floats_[ck] = (float)atof(val);
                else if (strings_.count(ck))
                    strings_[ck] = val;
                break;
            }
        }
    }
    fclose(f);
    LOGI("config: loaded from %s", path.c_str());
    return true;
}

bool Config::Save(const std::string& path) const {
    if (path.empty()) return false;
    FILE* f = fopen(path.c_str(), "w");
    if (!f) {
        LOGW("config: cannot write to %s", path.c_str());
        return false;
    }
    fprintf(f, "# XR Wrist Display v0.6.2 configuration\n");
    fprintf(f, "# Edit values, restart app to apply (most apply live).\n\n");
    std::lock_guard<std::mutex> lock(mutex_);
    for (int k = 0; k <= (int)ConfigKey::FirstRunComplete; ++k) {
        ConfigKey ck = (ConfigKey)k;
        const char* name = KeyName(ck);
        if (bools_.count(ck))
            fprintf(f, "%s=%d\n", name, bools_.at(ck) ? 1 : 0);
        else if (ints_.count(ck))
            fprintf(f, "%s=%d\n", name, ints_.at(ck));
        else if (floats_.count(ck))
            fprintf(f, "%s=%.4f\n", name, floats_.at(ck));
        else if (strings_.count(ck))
            fprintf(f, "%s=%s\n", name, strings_.at(ck).c_str());
    }
    fclose(f);
    return true;
}

}  // namespace xrwrist
