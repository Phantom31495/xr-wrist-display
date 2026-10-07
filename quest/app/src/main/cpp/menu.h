#pragma once
// v0.6.0: In-VR menu system for the XR Wrist Display.
//
// A floating, professionally-styled panel (Soft-Tech design system) toggled
// by a quick Y-button tap. Navigable via:
//   - controller ray (right aim) + trigger pull, and
//   - hand-tracking poke / pinch (XR_EXT_hand_interaction).
//
// Pages:
//   Main     — Stream start/stop, Settings, Environment, Diagnostics, About
//   Settings — display size, gaze dwell, voice, auto-transition, hands
//   About    — version / project info
//
// Human-centered: the panel floats 0.55m ahead, slightly below eye line,
// billboarded so no neck strain. All touch targets are far above the
// 48dp-equivalent minimum. Y-tap (<0.45s) toggles the menu; the existing
// Y-hold (>=0.8s) voice control is unaffected.

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <GLES3/gl3.h>

#include "devtools.h"
#include "gl_render.h"
#include "input.h"

namespace xrwrist {

// Callbacks into the app. Set once at Init; the menu owns no app state.
struct MenuActions {
    std::function<bool()> isStreaming;       // phone link active?
    std::function<void()> toggleStream;      // start/stop network
    std::function<void()> toggleDiagnostics; // godmode overlay
    std::function<bool()> isPassthrough;     // effective MR state
    std::function<void()> togglePassthrough; // VR <-> passthrough
    std::function<std::string()> versionString;
    // v0.6.2: projection mode (wrist / expanded / theater).
    std::function<std::string()> projectionModeName;
    std::function<void()> cycleProjectionMode;
};

class VrMenu {
public:
    VrMenu() = default;
    ~VrMenu() { Shutdown(); }

    bool Init(const MenuActions& actions);
    void Shutdown();

    void SetOpen(bool open);
    void Toggle() { SetOpen(!open_); }
    bool IsOpen() const { return open_; }

    // True when an open menu should swallow pointer input (so a trigger
    // pull on a menu button doesn't also send a touch to the phone).
    bool ConsumesInput() const;

    // Per-frame. dt: seconds. nowSec: monotonic clock for animations.
    void Update(float dt, const XrInput& in, const XrInput::Pose& head,
                float nowSec);

    // Draws the panel (call after the wrist display so the menu is on top).
    void Draw(const Mat4& viewProj, const Vec3& headPos);

    bool IsReady() const { return ok_; }

private:
    enum class Page { Main, Settings, About };

    struct Row {
        // Static label, or a function producing a dynamic label
        // (e.g. "Stream: Stop" vs "Stream: Start").
        std::string label;
        std::function<std::string()> dynLabel;
        std::function<void()> onActivate;
        bool selectable = true;  // false = info-only row
    };

    void RebuildRows();  // (re)build rows for the current page
    std::string RowLabel(size_t i) const;

    // Panel geometry for the current page. All in meters, panel-local
    // space: x right, y up, origin at panel center.
    float PanelWidth() const { return 0.36f; }
    float PanelHeight() const;
    float RowTopY(size_t i) const;  // top edge of row i
    float RowHeight() const { return 0.044f; }
    float RowGap() const { return 0.008f; }
    static constexpr float kHeaderH = 0.052f;
    static constexpr float kFooterH = 0.034f;
    static constexpr float kPad = 0.016f;

    void ComputePanelPose(const XrInput::Pose& head);
    // Returns hovered row index, or -1.
    int UpdateHover(const XrInput& in);
    void Activate(int row);

    // Rounded-rect plate drawing.
    bool InitPlateShader();
    void DrawPlate(const Mat4& viewProj, const Vec3& center, const Quat& quat,
                   float w, float h, const float color[4], float cornerR,
                   float borderW, const float borderColor[4]);
    void DrawAimRay(const Mat4& viewProj, const Vec3& origin,
                    const Vec3& dir, float length);

    MenuActions actions_;
    Page page_ = Page::Main;
    std::vector<Row> rows_;

    bool open_ = false;
    bool ok_ = false;
    float openT_ = 0.0f;  // seconds since opened (open animation)

    // Panel pose (updated in Update, used by Draw).
    Vec3 panelPos_;
    Quat panelQuat_;
    bool panelValid_ = false;

    // Input edge state.
    bool prevYDown_ = false;
    float yPressT_ = -10.0f;
    float prevTrigger_ = 0.0f;
    float prevPinchR_ = 0.0f;
    float prevPinchL_ = 0.0f;

    int hover_ = -1;       // currently hovered row
    int pressed_ = -1;     // row with active press animation
    float pressT_ = 10.0f; // seconds since press (for scale pop)

    // Text: one overlay per row + header + footer.
    devtools::TextOverlay headerOverlay_;
    devtools::TextOverlay footerOverlay_;
    std::vector<std::unique_ptr<devtools::TextOverlay>> rowOverlays_;

    // Plate shader + unit quad.
    GLuint plateProg_ = 0;
    GLuint plateVao_ = 0, plateVbo_ = 0;
    GLint plateUMvp_ = -1;
    GLint plateUColor_ = -1;
    GLint plateUCorner_ = -1;
    GLint plateUBorder_ = -1;
    GLint plateUBorderColor_ = -1;
    GLint plateUAspect_ = -1;
    // Aim ray.
    GLuint rayVao_ = 0, rayVbo_ = 0;
    GLuint rayProg_ = 0;
    GLint rayUMvp_ = -1;
    GLint rayUColor_ = -1;
    Vec3 rayOrigin_, rayDir_;
    float rayLen_ = 0.0f;
    bool rayValid_ = false;
};

}  // namespace xrwrist
