#pragma once
// XR development tools for the XR Wrist Display Quest app.
//
// Modular components, each usable standalone; the future Godmode dev console
// drives them through these APIs:
//
//   FrameDiagnostics  - FPS + CPU frame-time stats (Begin/End per frame)
//   DeviceInfo        - runtime version, extensions, refresh rates, views
//   InputSnapshot     - one-frame capture of all controller state
//   NetworkProbe      - async LAN discovery scan + TCP connection tester
//   TextOverlay       - toggleable world-space diagnostics panel (8x8 font)
//
// No third-party dependencies beyond the vendored public-domain font8x8.

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <GLES3/gl3.h>
#include <openxr/openxr.h>

#include "gl_render.h"
#include "input.h"

namespace xrwrist {
namespace devtools {

// ---------------------------------------------------------------------------
// FrameDiagnostics: FPS + CPU frame timing. Thread-affine (render thread).
// ---------------------------------------------------------------------------
class FrameDiagnostics {
public:
    void BeginFrame();
    void EndFrame();  // call after the frame's xrEndFrame is submitted

    double fps() const { return fps_; }
    double frameMsAvg() const { return msAvg_; }
    double frameMsWorst() const { return msWorst_; }  // worst of last 90
    uint64_t frameIndex() const { return frameIndex_; }

private:
    static constexpr int kWindow = 90;
    double times_[kWindow] = {};
    int head_ = 0;
    int count_ = 0;
    double fps_ = 0.0;
    double msAvg_ = 0.0;
    double msWorst_ = 0.0;
    uint64_t frameIndex_ = 0;
    uint64_t t0ns_ = 0;
    uint64_t lastEndNs_ = 0;
};

// ---------------------------------------------------------------------------
// DeviceInfo: XR runtime / device capabilities, collected once at init.
// ---------------------------------------------------------------------------
struct DeviceInfo {
    std::string runtimeName;     // e.g. "Oculus XR Runtime"
    std::string runtimeVersion;  // "major.minor.patch"
    std::vector<std::string> enabledExtensions;
    std::vector<float> refreshRates;  // Hz offered, may be empty
    uint32_t viewCount = 0;
    int viewW = 0;
    int viewH = 0;
    // Capability flags (probed, not just enabled).
    bool handTracking = false;
    bool passthrough = false;
    bool handMesh = false;
    bool handAim = false;
    bool refreshRate = false;

    std::string ToString() const;  // multi-line, for the overlay / logcat
};

DeviceInfo CollectDeviceInfo(XrInstance instance, XrSystemId systemId,
                             XrSession session,
                             const std::vector<const char*>& enabledExts,
                             uint32_t viewCount, int viewW, int viewH,
                             bool handTracking, bool passthrough,
                             bool handMesh, bool handAim, bool refreshRate);

// ---------------------------------------------------------------------------
// InputSnapshot: one-frame capture of every controller input, for the input
// debugger panel. Capture() reads the already-polled XrInput (no XR calls).
// ---------------------------------------------------------------------------
struct InputSnapshot {
    bool leftValid = false;
    bool rightValid = false;
    bool aimValid = false;
    float trigL = 0.0f, trigR = 0.0f;
    float squeezeL = 0.0f, squeezeR = 0.0f;
    float stickLX = 0.0f, stickLY = 0.0f;
    float stickRX = 0.0f, stickRY = 0.0f;
    bool x = false, y = false, a = false, b = false;

    static InputSnapshot Capture(const XrInput& in);
    // Compact multi-line rendering into the overlay.
    void AppendTo(std::string& out) const;
};

// ---------------------------------------------------------------------------
// NetworkProbe: async LAN discovery scan + TCP connection tester.
// The scan runs on a background thread; poll ScanDone() / TakeResults().
// ---------------------------------------------------------------------------
class NetworkProbe {
public:
    struct TcpTest {
        bool ok = false;
        int64_t connectUs = -1;
        std::string error;
    };
    struct Host {
        std::string ip;
        int64_t latencyUs = -1;  // probe -> response
        TcpTest video;           // port 8900, tested after scan
        TcpTest control;         // port 8901, tested after scan
    };

    NetworkProbe() = default;
    ~NetworkProbe() { StopScan(); }
    NetworkProbe(const NetworkProbe&) = delete;
    NetworkProbe& operator=(const NetworkProbe&) = delete;

    // Broadcasts discovery probes for timeoutMs, collecting every responder,
    // then TCP-tests the video/control ports of each (all off-thread).
    void StartScan(int timeoutMs = 1500);
    void StopScan();
    bool ScanDone() const { return scanDone_.load(); }
    std::vector<Host> TakeResults();  // valid once ScanDone()

    static TcpTest TestTcp(const std::string& ip, int port,
                           int timeoutMs = 1200);

private:
    void ScanThread(int timeoutMs);

    std::atomic<bool> scanDone_{true};
    std::atomic<bool> stop_{false};
    std::thread thread_;
    std::mutex mutex_;
    std::vector<Host> hosts_;
};

// ---------------------------------------------------------------------------
// TextOverlay: toggleable world-space diagnostics panel.
// Monospace 8x8 bitmap font (public domain font8x8), single draw call,
// CPU-rasterized vertex buffer rebuilt only when content changes.
// ---------------------------------------------------------------------------
class TextOverlay {
public:
    static constexpr int kMaxLines = 24;
    static constexpr int kMaxCols = 44;

    TextOverlay() = default;
    ~TextOverlay() { Shutdown(); }

    bool Init();
    void Shutdown();

    void Clear();
    void Printf(const char* fmt, ...);
    bool HasContent() const { return !text_.empty(); }

    // v0.6.0: menu styling — override the background plate color, or
    // disable the plate entirely (for text drawn over custom plates).
    void SetBackgroundColor(float r, float g, float b, float a) {
        bgColor_[0] = r;
        bgColor_[1] = g;
        bgColor_[2] = b;
        bgColor_[3] = a;
    }
    void SetBackgroundEnabled(bool enabled) { bgEnabled_ = enabled; }

    // panelW: panel width in meters (height follows the text aspect).
    // Call every frame the panel should be visible.
    void Draw(const Mat4& viewProj, const Mat4& panelModel, float panelW);

private:
    bool BuildAtlas();
    void RebuildMesh();  // uploads quads for current text_

    GLuint atlasTex_ = 0;
    GLuint prog_ = 0;
    GLuint bgProg_ = 0;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint bgVao_ = 0;  // persistent unit quad for the background plate
    GLuint bgVbo_ = 0;
    GLint uMvp_ = -1;
    GLint uTex_ = -1;
    GLint uBgMvp_ = -1;
    GLint uBgColor_ = -1;

    std::string text_;  // '\n'-separated lines
    bool dirty_ = true;
    int quadCount_ = 0;
    bool ok_ = false;
    // v0.6.0: background plate styling (defaults = original look).
    float bgColor_[4] = {0.02f, 0.03f, 0.04f, 0.72f};
    bool bgEnabled_ = true;
};

}  // namespace devtools
}  // namespace xrwrist
