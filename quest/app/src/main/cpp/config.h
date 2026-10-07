#pragma once
// v0.5.0: Production configuration system.
//
// Every tunable in the app lives here — no hardcoded values in feature code.
// Settings persist to a file and apply live (no restart needed).
//
// Design:
// - Config is a singleton, thread-safe for reads.
// - Values are typed (bool, int, float, string).
// - Changes notify registered listeners (for live-apply).
// - Persists to app-private storage as JSON-ish key=value.

#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace xrwrist {

// All tunable keys. Adding a new setting = add a key here + default below.
enum class ConfigKey {
    // Network
    DiscoveryPort,       // int: UDP discovery port (default 8899)
    VideoPort,           // int: TCP video port (default 8900)
    ControlPort,         // int: TCP control port (default 8901)
    DiscoveryTimeoutMs,  // int: discovery timeout (default 3000)
    ReconnectIntervalMs, // int: reconnect delay after drop (default 5000)
    PhoneIpOverride,     // string: manual IP, empty = auto-discover

    // Display
    GlanceSizeM,         // float: watch face width in meters (default 0.08)
    EngagedSizeM,        // float: world-locked panel width (default 0.42)
    GazeDwellSec,        // float: seconds to trigger engage (default 1.5)
    GazeLostSec,         // float: seconds to trigger disengage (default 2.0)
    WorldDistanceM,      // float: world-locked panel distance (default 0.7)
    WorldHeightOffsetM,  // float: below eye line (default -0.12)

    // Interaction
    AutoTransition,      // bool: posture-driven wrist->world (default true)
    WristRaiseSec,        // float: seconds to trigger auto-detach (default 4.0)
    TouchDistanceM,      // float: finger touch threshold (default 0.030)
    PinchThreshold,      // float: pinch click threshold 0..1 (default 0.8)
    VoiceEnabled,        // bool: voice commands (default true)

    // Avatar
    AvatarEnabled,       // bool: show avatar hands (default true)
    WatchGlowIntensity,  // float: pre-detach glow 0..1 (default 1.0)

    // Godmode
    GodmodeEnabled,      // bool: dev console available (default true)

    // v0.6.0: environment + menu
    PassthroughEnabled,  // bool: mixed-reality passthrough (default true)
    EnvironmentEnabled,  // bool: VR room backdrop (default true)

    // v0.6.2: phone-screen projection UI
    ProjectionMode,      // int: 0=wrist, 1=expanded, 2=theater (default 0)
    DisplayBrightness,   // float: video brightness 0.3..1.5 (default 1.0)
    OrientationLocked,   // bool: freeze panel world pose (default false)

    // System
    FirstRunComplete,    // bool: onboarding shown (default false)
};

class Config {
public:
    static Config& Instance();

    // Typed getters (thread-safe).
    bool GetBool(ConfigKey key) const;
    int GetInt(ConfigKey key) const;
    float GetFloat(ConfigKey key) const;
    std::string GetString(ConfigKey key) const;

    // Typed setters — persist immediately and notify listeners.
    void SetBool(ConfigKey key, bool value);
    void SetInt(ConfigKey key, int value);
    void SetFloat(ConfigKey key, float value);
    void SetString(ConfigKey key, const std::string& value);

    // Live-apply: called on the render thread each frame to drain changes.
    using ChangeCallback = std::function<void(ConfigKey)>;
    void AddListener(ChangeCallback cb);

    // Persistence.
    bool Load(const std::string& path);
    bool Save(const std::string& path) const;
    void SetPath(const std::string& path) { path_ = path; }

    // Reset all to defaults.
    void ResetDefaults();

private:
    Config() { ResetDefaults(); }
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    void NotifyChanged(ConfigKey key);

    mutable std::mutex mutex_;
    std::map<ConfigKey, bool> bools_;
    std::map<ConfigKey, int> ints_;
    std::map<ConfigKey, float> floats_;
    std::map<ConfigKey, std::string> strings_;
    std::vector<ChangeCallback> listeners_;
    std::string path_;
};

}  // namespace xrwrist
