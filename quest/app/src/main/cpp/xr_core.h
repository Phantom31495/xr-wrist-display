#pragma once
// Platform headers must come before OpenXR platform defines
#include <jni.h>
#include <EGL/egl.h>
// OpenXR platform defines must come before openxr.h
#define XR_USE_PLATFORM_ANDROID 1
#define XR_USE_GRAPHICS_API_OPENGL_ES 1
#define XR_EXTENSION_PROTOTYPES 1
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <EGL/egl.h>
#include <android_native_app_glue.h>

#include <atomic>
#include <string>
#include <vector>

#include "gl_render.h"
#include "input.h"
#include "net.h"
#include "video.h"
#include "avatar.h"
#include "devtools.h"
#include "environment.h"
#include "menu.h"
#include "projection.h"

namespace xrwrist {

enum class AnchorMode { WRIST, FIXED };

// v0.4.0: human-centered display states.
enum class DisplayZoom { GLANCE, EXPANDING, ENGAGED, SHRINKING };

// Owns the full OpenXR lifecycle: EGL, instance, session, swapchains,
// passthrough, input, video pipeline, and the per-frame render.
class XrApp {
public:
    XrApp() = default;
    ~XrApp() { Shutdown(); }

    bool Init(android_app* app);
    void Shutdown();

    // Pump Android + OpenXR events. Sets exitRequested when quitting.
    void PollEvents(bool& exitRequested);
    // Run one frame (no-op unless the XR session is running).
    void Frame();

    bool IsSessionRunning() const { return sessionRunning_; }

private:
    bool InitEgl();
    bool InitXrLoader();
    bool InitXrInstance();
    bool QueryGraphicsRequirements();
    bool InitXrSession();
    bool InitSwapchains();
    bool InitPassthrough();
    void InitRefreshRate();  // optional XR_FB_display_refresh_rate (90 Hz)
    void StartNetwork();  // background: discovery -> decoder -> clients
    void StopNetwork();   // v0.6.0: stop stream (menu toggle)
    bool IsStreaming() const;  // v0.6.0: any client running
    void ToggleDiagnostics();  // v0.6.0: godmode overlay (menu + B button)

    void HandleSessionState(XrSessionState state, bool& exitRequested);
    void ComputeQuadPose(const XrInput::Pose& head, Vec3& outPos, Quat& outQuat,
                         float& outW, float& outH);
    // v0.4.0: pose for an explicit anchor mode (ComputeQuadPose uses anchorMode_).
    void ComputeQuadPoseFor(AnchorMode mode, const XrInput::Pose& head,
                            Vec3& outPos, Quat& outQuat,
                            float& outW, float& outH);
    void ComputeDiagPanelPose(const XrInput::Pose& head, Vec3& outPos,
                              Quat& outQuat);
    void BuildDiagContent();  // fills diagOverlay_ (once per frame)
    void HandleTouch(const XrInput::Pose& head, const Vec3& quadPos,
                     const Quat& quadQuat, float quadW, float quadH);
    // v0.4.0: human-centered interaction.
    void UpdateDynamicZoom(const XrInput::Pose& head, const Vec3& dispPos,
                           const Quat& dispQuat, float dispW, float dispH,
                           float dt);
    void UpdateAutoTransition(const XrInput::Pose& head, float dt);
    void HandleFingerTouch(const Vec3& quadPos, const Quat& quadQuat,
                           float quadW, float quadH);
    void UpdateVoice(float dt);
    void ExecuteVoiceCommand(const std::string& cmd);
    void RenderLayer(XrTime predictedTime);

    android_app* app_ = nullptr;

    // EGL
    EGLDisplay eglDisplay_ = EGL_NO_DISPLAY;
    EGLConfig eglConfig_ = nullptr;
    EGLContext eglContext_ = EGL_NO_CONTEXT;
    EGLSurface eglPbuffer_ = EGL_NO_SURFACE;
    // Minimum OpenGL ES minor version required by the XR runtime (major is 3).
    int glEsMinorRequired_ = 0;

    // OpenXR
    XrInstance instance_ = XR_NULL_HANDLE;
    XrSystemId systemId_ = XR_NULL_SYSTEM_ID;
    XrSession session_ = XR_NULL_HANDLE;
    XrSessionState sessionState_ = XR_SESSION_STATE_UNKNOWN;
    bool sessionRunning_ = false;
    bool sessionBegun_ = false;  // xrBeginSession called without matching xrEndSession
    XrSpace localSpace_ = XR_NULL_HANDLE;
    XrSpace viewSpace_ = XR_NULL_HANDLE;
    uint32_t viewCount_ = 0;
    std::vector<XrView> views_;

    struct SwapchainInfo {
        XrSwapchain handle = XR_NULL_HANDLE;
        int32_t width = 0;
        int32_t height = 0;
        std::vector<XrSwapchainImageOpenGLESKHR> images;
        std::vector<GLuint> fbos;  // one FBO per image
        GLuint depthRbo = 0;       // shared depth buffer (cleared per frame)
    };
    std::vector<SwapchainInfo> swapchains_;

    // Passthrough (XR_FB_passthrough)
    PFN_xrCreatePassthroughFB xrCreatePassthroughFB_ = nullptr;
    PFN_xrDestroyPassthroughFB xrDestroyPassthroughFB_ = nullptr;
    PFN_xrPassthroughStartFB xrPassthroughStartFB_ = nullptr;
    PFN_xrPassthroughPauseFB xrPassthroughPauseFB_ = nullptr;
    PFN_xrCreatePassthroughLayerFB xrCreatePassthroughLayerFB_ = nullptr;
    PFN_xrDestroyPassthroughLayerFB xrDestroyPassthroughLayerFB_ = nullptr;
    PFN_xrPassthroughLayerResumeFB xrPassthroughLayerResumeFB_ = nullptr;
    XrPassthroughFB passthrough_ = XR_NULL_HANDLE;
    XrPassthroughLayerFB passthroughLayer_ = XR_NULL_HANDLE;
    bool passthroughSupported_ = false;
    bool handTrackingSupported_ = false;
    bool refreshRateSupported_ = false;
    bool handMeshSupported_ = false;  // informational (device info)
    bool handAimSupported_ = false;   // informational (device info)

    // Subsystems
    XrInput input_;
    QuadRenderer quadRenderer_;
    AvatarRenderer avatar_;
    bool avatarReady_ = false;

    // XR dev tools (Godmode foundation): diagnostics, overlay, net probe.
    devtools::FrameDiagnostics frameDiag_;
    devtools::DeviceInfo deviceInfo_;
    devtools::TextOverlay diagOverlay_;
    devtools::NetworkProbe netProbe_;
    // v0.5.0: user-facing notification toasts (human-readable errors).
    devtools::TextOverlay notifyOverlay_;
    AvatarRenderer::RenderStats lastAvatarStats_;
    bool showDiag_ = false;
    GLuint videoTex_ = 0;  // GL_TEXTURE_EXTERNAL_OES
    SurfaceTextureHelper surfaceTexture_;
    VideoDecoder decoder_;
    VideoClient videoClient_;
    ControlClient controlClient_;
    std::thread netThread_;

    std::string phoneIp_;
    int videoW_ = 720;
    int videoH_ = 1280;
    bool videoReady_ = false;
    ANativeWindow* decoderWindow_ = nullptr;

    AnchorMode anchorMode_ = AnchorMode::WRIST;
    bool touchDown_ = false;
    float lastU_ = 0.0f, lastV_ = 0.0f;
    bool screenOffSent_ = false;
    uint64_t frameCount_ = 0;

    // ---- v0.4.0: dynamic sizing (glance <-> engage) ----
    DisplayZoom zoom_ = DisplayZoom::GLANCE;
    float zoomT_ = 0.0f;        // 0 = glance size, 1 = engaged size
    float gazeDwell_ = 0.0f;    // seconds of sustained gaze on the display
    float gazeLost_ = 0.0f;     // seconds since gaze left (in ENGAGED)
    // ---- v0.4.0: auto wrist->fixed transition ----
    float wristRaiseDwell_ = 0.0f;  // seconds wrist held in reading posture
    float detachT_ = 0.0f;         // 0 = wrist-anchored, 1 = fully detached
    bool detaching_ = false;       // transition animation running
    bool autoTransition_ = true;   // user-toggleable via voice
    bool detachHintShown_ = false; // first-time onboarding hint
    float watchGlow_ = 0.0f;       // pre-detach warning glow 0..1
    // ---- v0.4.0: direct finger touch ----
    bool fingerTouchDown_ = false;
    float fingerU_ = 0.0f, fingerV_ = 0.0f;
    float touchPulseT_ = 0.0f;  // touch ripple visual, seconds since touch
    float touchPulseU_ = 0.0f, touchPulseV_ = 0.5f;
    // ---- v0.4.0: voice control ----
    bool voiceListening_ = false;
    float voiceHoldT_ = 0.0f;  // Y-hold time to trigger listening
    std::string lastHeard_;
    float lastHeardT_ = -100.0f;  // frame-time of last recognition
    XrTime lastPredictedTime_ = 0;  // for per-frame dt
    class VoiceBridge* voice_ = nullptr;  // JNI speech recognizer (voice.h)
    devtools::TextOverlay voiceOverlay_;  // mic pill while listening
    float detachTarget_ = 0.0f;  // animated anchor blend target (0/1)
    bool wristWasLowered_ = false;  // re-attach gesture state
    // v0.5.0 (SG-1): track whether the current detach was caused by engage
    // (vs manual X+Y or auto-transition). If engage caused it, disengaging
    // returns the panel to the wrist.
    bool engageDetached_ = false;
    // ---- v0.6.0: VR environment + in-VR menu ----
    EnvironmentRenderer env_;
    bool envReady_ = false;
    VrMenu menu_;
    // ---- v0.6.2: phone-screen projection UI ----
    ProjectionUI projUI_;
    bool projUiReady_ = false;
    uint64_t lastVideoFrames_ = 0;  // for fps measurement
    float videoFps_ = 0.0f;
    float fpsWindowT_ = 0.0f;
    bool wasStreaming_ = false;  // v0.6.2: drop watchdog
    std::atomic<int> netGen_{0};  // StartNetwork generation (for Stop)
};

}  // namespace xrwrist
