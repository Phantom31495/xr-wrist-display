#pragma once
// v0.6.2: Phone-screen projection UI for the XR Wrist Display.
//
// Professional UI elements around the projected phone screen:
//   1. Accent frame — warm-amber trim ring + soft drop shadow + status dot,
//      giving the projection the feel of a premium physical device.
//   2. Control bar — glanceable brightness / zoom / orientation-lock
//      controls. Revealed by gazing at the display, auto-hides when
//      ignored (like a real watch: there when you look, gone when not).
//   3. Projection modes — WRIST (current), EXPANDED (large float panel),
//      THEATER (cinema-size panel for media). Smooth animated transitions.
//   4. Touch feedback — the existing ripple is tinted amber; a persistent
//      touch dot shows while a finger is down.
//   5. Status indicators — connection-quality dot on the frame (subtle),
//      video fps + resolution in the tech panel, human-readable warnings
//      via the existing notification system.
//
// Human-centered: everything is glanceable, high-contrast, plain-language.
// Soft-Tech palette throughout: charcoal, warm amber, no pure black/white.

#include <string>

#include <GLES3/gl3.h>

#include "devtools.h"
#include "gl_render.h"
#include "input.h"

namespace xrwrist {

enum class ProjectionMode { WRIST = 0, EXPANDED = 1, THEATER = 2 };

const char* ProjectionModeName(ProjectionMode m);

// Owns frame dressing, control bar, and mode transitions for the phone
// screen projection. Stateless w.r.t. the app; the app feeds it the
// per-frame display pose and stream health.
class ProjectionUI {
public:
    ProjectionUI() = default;
    ~ProjectionUI() { Shutdown(); }

    bool Init();
    void Shutdown();
    bool IsReady() const { return ok_; }

    // ---- projection modes ----
    void SetMode(ProjectionMode m);
    void CycleMode();  // WRIST -> EXPANDED -> THEATER -> WRIST
    ProjectionMode GetMode() const { return mode_; }

    // Adjusts dispPos/dispQuat/dispW/dispH for the current projection
    // mode, with a smooth animated transition. Call after the app computes
    // the base display pose each frame. In WRIST mode this is a
    // pass-through (plus orientation-lock handling).
    void ApplyProjectionMode(const XrInput::Pose& head, Vec3& dispPos,
                             Quat& dispQuat, float& dispW, float& dispH,
                             float dt);

    // ---- orientation lock ----
    // When locked, the panel keeps a fixed world pose instead of
    // following the wrist/head every frame.
    void SetOrientationLocked(bool locked, const Vec3& curPos,
                              const Quat& curQuat);
    bool IsOrientationLocked() const { return orientationLocked_; }

    // ---- brightness ----
    float GetBrightness() const { return brightness_; }
    void SetBrightness(float b);       // clamped 0.3 .. 1.5
    void AdjustBrightness(float delta);

    // ---- per-frame update ----
    // gazeOnDisplay: is the user's gaze on the display this frame.
    // videoFps: measured decoder fps (0 when not streaming).
    // streaming: is the phone link active.
    // touchDown/touchU/touchV: active finger-touch state for the dot.
    void Update(float dt, const XrInput& in, const XrInput::Pose& head,
                const Vec3& dispPos, const Quat& dispQuat, float dispW,
                float dispH, bool gazeOnDisplay, float videoFps,
                bool streaming, bool touchDown, float touchU, float touchV,
                float nowSec);

    // Returns true when the control bar is visible and the pointer is
    // over it — the app should skip phone-touch handling (same contract
    // as VrMenu::ConsumesInput).
    bool ConsumesInput() const;

    // ---- drawing (call in render, after the display itself) ----
    // Frame dressing: shadow, accent ring, status dot.
    void DrawFrame(const Mat4& viewProj, const Vec3& dispPos,
                   const Quat& dispQuat, float dispW, float dispH,
                   float videoFps, bool streaming);
    // Control bar (no-op when hidden).
    void DrawControls(const Mat4& viewProj, const Vec3& headPos);

private:
    // Control bar buttons.
    enum class CtlButton { None = -1, Dim = 0, Brighten = 1, Lock = 2 };

    void ComputeControlBarPose(const Vec3& dispPos, const Quat& dispQuat,
                               float dispW, float dispH);
    CtlButton HitTestControls(const Vec3& origin, const Vec3& dir) const;
    void ActivateControl(CtlButton b);
    const char* ControlLabel(CtlButton b) const;

    // Rounded-rect drawing (plate shader, shared pattern with VrMenu).
    bool InitPlateShader();
    void DrawPlate(const Mat4& viewProj, const Vec3& center, const Quat& quat,
                   float w, float h, const float color[4], float cornerR);
    void DrawRing(const Mat4& viewProj, const Vec3& center, const Quat& quat,
                  float w, float h, const float color[4], float thickness);
    void DrawDot(const Mat4& viewProj, const Vec3& center, const Quat& quat,
                 float diameter, const float color[4]);

    // Mode transition targets.
    void ModeTarget(const XrInput::Pose& head, float baseW, float baseH,
                    Vec3& outPos, Quat& outQuat, float& outW, float& outH);

    bool ok_ = false;

    // Mode state.
    ProjectionMode mode_ = ProjectionMode::WRIST;
    float modeBlend_ = 1.0f;  // 0..1 transition progress
    Vec3 modeFromPos_, modeToPos_;
    Quat modeFromQuat_, modeToQuat_;
    float modeFromW_ = 0, modeFromH_ = 0, modeToW_ = 0, modeToH_ = 0;
    Vec3 lastOutPos_;
    Quat lastOutQuat_;
    float lastOutW_ = 0, lastOutH_ = 0;
    bool haveLastOut_ = false;

    // Orientation lock.
    bool orientationLocked_ = false;
    Vec3 lockedPos_;
    Quat lockedQuat_;

    // Brightness (applied to the glass shader by the app).
    float brightness_ = 1.0f;

    // Control bar state.
    bool controlsVisible_ = false;
    float controlsT_ = 0.0f;      // seconds since shown (for fade)
    float gazeDwellCtl_ = 0.0f;   // gaze dwell accumulator
    float hideT_ = 0.0f;          // seconds since gaze left
    Vec3 barPos_;
    Quat barQuat_;
    float barW_ = 0, barH_ = 0;
    bool barValid_ = false;
    int hoverCtl_ = -1;
    float prevTrigger_ = 0.0f;
    float prevPinchR_ = 0.0f;

    // Touch dot state.
    bool touchDotOn_ = false;
    float touchDotU_ = 0.5f, touchDotV_ = 0.5f;

    // Status.
    float lastFps_ = 0.0f;
    bool lastStreaming_ = false;
    float pulseT_ = 0.0f;  // monotonic, for the connecting pulse

    // Plate shader + unit quad.
    GLuint plateProg_ = 0;
    GLuint plateVao_ = 0, plateVbo_ = 0;
    GLint plateUMvp_ = -1;
    GLint plateUColor_ = -1;
    GLint plateUCorner_ = -1;
    GLint plateUBorder_ = -1;
    GLint plateUBorderColor_ = -1;
    GLint plateUAspect_ = -1;

    // Labels.
    devtools::TextOverlay labelOverlay_;
};

}  // namespace xrwrist
