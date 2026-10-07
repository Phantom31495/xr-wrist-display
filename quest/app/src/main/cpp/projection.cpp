#include "projection.h"

#include <cmath>
#include <cstdio>

#include "config.h"
#include "log.h"
#include "notify.h"

namespace xrwrist {
namespace {

// ---- Soft-Tech palette (charcoal + warm amber, no pure black/white) ----
constexpr float kAmber[4] = {1.0f, 0.62f, 0.15f, 1.0f};
constexpr float kCharcoal[4] = {0.09f, 0.10f, 0.12f, 0.94f};
constexpr float kShadowCol[4] = {0.015f, 0.016f, 0.022f, 0.42f};
constexpr float kStatusGood[4] = {0.55f, 0.85f, 0.60f, 1.0f};
constexpr float kStatusDegraded[4] = {1.0f, 0.62f, 0.15f, 1.0f};
constexpr float kStatusLost[4] = {0.95f, 0.42f, 0.28f, 1.0f};
constexpr float kStatusIdle[4] = {0.35f, 0.37f, 0.42f, 1.0f};

// Control bar layout (meters, bar-local space, origin at bar center).
constexpr float kBtnW = 0.062f;
constexpr float kBtnH = 0.034f;
constexpr float kBtnGap = 0.008f;
constexpr float kBarPad = 0.010f;
constexpr int kNumButtons = 3;

// Gaze reveal: look at the display this long to show controls.
constexpr float kRevealDwellSec = 0.8f;
// Auto-hide after the gaze leaves for this long.
constexpr float kHideDelaySec = 3.0f;
// Fade in/out time.
constexpr float kFadeSec = 0.25f;
// Mode transition duration.
constexpr float kModeTransitionSec = 0.6f;

float Clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
float Smoothstep(float t) {
    t = Clamp01(t);
    return t * t * (3.0f - 2.0f * t);
}

Quat NlerpQ(const Quat& a, const Quat& b, float t) {
    float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    float s = dot < 0 ? -1.0f : 1.0f;
    Quat r{a.x + s * t * (b.x - a.x), a.y + s * t * (b.y - a.y),
           a.z + s * t * (b.z - a.z), a.w + s * t * (b.w - a.w)};
    float len = sqrtf(r.x * r.x + r.y * r.y + r.z * r.z + r.w * r.w);
    if (len > 1e-6f) {
        r.x /= len;
        r.y /= len;
        r.z /= len;
        r.w /= len;
    }
    return r;
}

// Billboard: local +Z faces the head.
Quat BillboardQ(const Vec3& headPos, const Vec3& objPos) {
    Vec3 zAxis = (headPos - objPos).normalized();
    Vec3 xAxis = Vec3(0, 1, 0).cross(zAxis).normalized();
    if (xAxis.length() < 1e-4f) xAxis = Vec3(1, 0, 0);
    Vec3 yAxis = zAxis.cross(xAxis);
    float m00 = xAxis.x, m01 = yAxis.x, m02 = zAxis.x;
    float m10 = xAxis.y, m11 = yAxis.y, m12 = zAxis.y;
    float m20 = xAxis.z, m21 = yAxis.z, m22 = zAxis.z;
    float tr = m00 + m11 + m22;
    Quat q;
    if (tr > 0) {
        float s = sqrtf(tr + 1.0f) * 2.0f;
        q.w = 0.25f * s;
        q.x = (m21 - m12) / s;
        q.y = (m02 - m20) / s;
        q.z = (m10 - m01) / s;
    } else if (m00 > m11 && m00 > m22) {
        float s = sqrtf(1.0f + m00 - m11 - m22) * 2.0f;
        q.w = (m21 - m12) / s;
        q.x = 0.25f * s;
        q.y = (m01 + m10) / s;
        q.z = (m02 + m20) / s;
    } else if (m11 > m22) {
        float s = sqrtf(1.0f + m11 - m00 - m22) * 2.0f;
        q.w = (m02 - m20) / s;
        q.x = (m01 + m10) / s;
        q.y = 0.25f * s;
        q.z = (m12 + m21) / s;
    } else {
        float s = sqrtf(1.0f + m22 - m00 - m11) * 2.0f;
        q.w = (m10 - m01) / s;
        q.x = (m02 + m20) / s;
        q.y = (m12 + m21) / s;
        q.z = 0.25f * s;
    }
    return q;
}

GLuint LinkProgram(const char* vs, const char* fs) {
    auto compile = [](GLenum type, const char* src) -> GLuint {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[1024];
            glGetShaderInfoLog(s, sizeof(log), nullptr, log);
            LOGE("projection: shader compile failed: %s", log);
            glDeleteShader(s);
            return 0;
        }
        return s;
    };
    GLuint v = compile(GL_VERTEX_SHADER, vs);
    GLuint f = compile(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) {
        if (v) glDeleteShader(v);
        if (f) glDeleteShader(f);
        return 0;
    }
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        LOGE("projection: program link failed");
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

// Rounded-rect plate shader (same SDF pattern as the in-VR menu).
const char* kPlateVert = R"(
#version 300 es
layout(location=0) in vec2 aPos;  // [0,1]x[0,1]
uniform mat4 uMvp;
out vec2 vUv;
void main() {
    vUv = aPos;
    gl_Position = uMvp * vec4(aPos, 0.0, 1.0);
}
)";

const char* kPlateFrag = R"(
#version 300 es
precision mediump float;
in vec2 vUv;
uniform vec4 uColor;
uniform float uCorner;   // corner radius in UV units (of min dimension)
uniform float uBorder;   // border width in UV units (0 = filled)
uniform vec4 uBorderColor;
uniform float uAspect;   // width / height
out vec4 oColor;
float sdRoundBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}
void main() {
    vec2 p = vec2(vUv.x * uAspect, vUv.y);
    vec2 b = vec2(uAspect * 0.5, 0.5);
    float d = sdRoundBox(p - vec2(uAspect * 0.5, 0.5), b - uCorner, uCorner);
    float alpha = 1.0 - smoothstep(-0.004, 0.004, d);
    if (alpha < 0.01) discard;
    vec3 col = uColor.rgb;
    float a = uColor.a * alpha;
    if (uBorder > 0.0001) {
        // Ring mode: keep only the border band, discard the interior.
        float band = 1.0 - smoothstep(uBorder * 0.5, uBorder * 0.5 + 0.004, abs(d));
        if (band < 0.01) discard;
        col = uBorderColor.rgb;
        a = uBorderColor.a * band;
    }
    oColor = vec4(col, a);
}
)";

}  // namespace

const char* ProjectionModeName(ProjectionMode m) {
    switch (m) {
        case ProjectionMode::WRIST: return "Wrist";
        case ProjectionMode::EXPANDED: return "Expanded";
        case ProjectionMode::THEATER: return "Theater";
    }
    return "Wrist";
}

bool ProjectionUI::Init() {
    if (!InitPlateShader()) {
        LOGE("projection: plate shader init failed");
        return false;
    }
    if (!labelOverlay_.Init()) {
        LOGE("projection: label overlay init failed");
        return false;
    }
    labelOverlay_.SetBackgroundEnabled(false);

    // Restore persisted settings.
    auto& cfg = Config::Instance();
    int mode = cfg.GetInt(ConfigKey::ProjectionMode);
    if (mode >= 0 && mode <= 2) mode_ = (ProjectionMode)mode;
    float b = cfg.GetFloat(ConfigKey::DisplayBrightness);
    if (b >= 0.3f && b <= 1.5f) brightness_ = b;
    orientationLocked_ = cfg.GetBool(ConfigKey::OrientationLocked);

    modeBlend_ = 1.0f;  // start settled
    ok_ = true;
    LOGI("projection: init ok (mode=%s brightness=%.2f)",
         ProjectionModeName(mode_), brightness_);
    return true;
}

void ProjectionUI::Shutdown() {
    if (plateProg_) glDeleteProgram(plateProg_);
    if (plateVao_) glDeleteVertexArrays(1, &plateVao_);
    if (plateVbo_) glDeleteBuffers(1, &plateVbo_);
    plateProg_ = 0;
    plateVao_ = plateVbo_ = 0;
    labelOverlay_.Shutdown();
    ok_ = false;
}

bool ProjectionUI::InitPlateShader() {
    plateProg_ = LinkProgram(kPlateVert, kPlateFrag);
    if (!plateProg_) return false;
    plateUMvp_ = glGetUniformLocation(plateProg_, "uMvp");
    plateUColor_ = glGetUniformLocation(plateProg_, "uColor");
    plateUCorner_ = glGetUniformLocation(plateProg_, "uCorner");
    plateUBorder_ = glGetUniformLocation(plateProg_, "uBorder");
    plateUBorderColor_ = glGetUniformLocation(plateProg_, "uBorderColor");
    plateUAspect_ = glGetUniformLocation(plateProg_, "uAspect");

    float q[] = {0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1};
    glGenVertexArrays(1, &plateVao_);
    glGenBuffers(1, &plateVbo_);
    glBindVertexArray(plateVao_);
    glBindBuffer(GL_ARRAY_BUFFER, plateVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(q), q, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (void*)0);
    glBindVertexArray(0);
    return true;
}

void ProjectionUI::SetMode(ProjectionMode m) {
    if (m == mode_) return;
    mode_ = m;
    modeBlend_ = 0.0f;  // capture "from" on the next ApplyProjectionMode
    Config::Instance().SetInt(ConfigKey::ProjectionMode, (int)m);
    char msg[64];
    snprintf(msg, sizeof(msg), "Projection: %s", ProjectionModeName(m));
    Notify::Instance().Info(msg);
    LOGI("projection: mode -> %s", ProjectionModeName(m));
}

void ProjectionUI::CycleMode() {
    SetMode(mode_ == ProjectionMode::WRIST      ? ProjectionMode::EXPANDED
            : mode_ == ProjectionMode::EXPANDED ? ProjectionMode::THEATER
                                               : ProjectionMode::WRIST);
}

void ProjectionUI::SetBrightness(float b) {
    brightness_ = b < 0.3f ? 0.3f : (b > 1.5f ? 1.5f : b);
    Config::Instance().SetFloat(ConfigKey::DisplayBrightness, brightness_);
}

void ProjectionUI::AdjustBrightness(float delta) {
    SetBrightness(brightness_ + delta);
    char msg[48];
    snprintf(msg, sizeof(msg), "Brightness %d%%", (int)(brightness_ * 100));
    Notify::Instance().Info(msg);
}

void ProjectionUI::SetOrientationLocked(bool locked, const Vec3& curPos,
                                        const Quat& curQuat) {
    orientationLocked_ = locked;
    if (locked) {
        lockedPos_ = curPos;
        lockedQuat_ = curQuat;
        Notify::Instance().Info("Display locked in place",
                                "It stays here until you unlock it");
    } else {
        Notify::Instance().Info("Display unlocked",
                                "It follows your wrist again");
    }
    Config::Instance().SetBool(ConfigKey::OrientationLocked, locked);
}

void ProjectionUI::ModeTarget(const XrInput::Pose& head, float baseW,
                              float baseH, Vec3& outPos, Quat& outQuat,
                              float& outW, float& outH) {
    float aspect = baseH > 1e-6f ? baseW / baseH : 9.0f / 16.0f;
    Vec3 fwd = head.quat.rotate(Vec3(0, 0, -1));
    Vec3 up = head.quat.rotate(Vec3(0, 1, 0));
    if (mode_ == ProjectionMode::EXPANDED) {
        // Large floating panel at a comfortable reading distance.
        outW = 0.84f;
        outH = outW / aspect;
        outPos = head.pos + fwd * 0.9f + up * -0.05f;
        outQuat = BillboardQ(head.pos, outPos);
    } else if (mode_ == ProjectionMode::THEATER) {
        // Cinema-size panel for media, further out, slightly below eye line.
        outW = 1.40f;
        outH = outW / aspect;
        outPos = head.pos + fwd * 2.2f + up * -0.25f;
        outQuat = BillboardQ(head.pos, outPos);
    } else {
        // WRIST: caller keeps the computed pose (pass-through).
        outW = baseW;
        outH = baseH;
    }
}

void ProjectionUI::ApplyProjectionMode(const XrInput::Pose& head,
                                       Vec3& dispPos, Quat& dispQuat,
                                       float& dispW, float& dispH, float dt) {
    // Orientation lock: freeze the world pose (size still follows mode).
    if (orientationLocked_) {
        dispPos = lockedPos_;
        dispQuat = lockedQuat_;
    }

    if (mode_ == ProjectionMode::WRIST) {
        modeBlend_ = 1.0f;
    } else {
        // Capture the "from" state on the first frame after a mode change.
        if (modeBlend_ <= 0.0f && haveLastOut_) {
            modeFromPos_ = lastOutPos_;
            modeFromQuat_ = lastOutQuat_;
            modeFromW_ = lastOutW_;
            modeFromH_ = lastOutH_;
        }
        Vec3 toPos;
        Quat toQuat;
        float toW, toH;
        ModeTarget(head, dispW, dispH, toPos, toQuat, toW, toH);
        modeToPos_ = toPos;
        modeToQuat_ = toQuat;
        modeToW_ = toW;
        modeToH_ = toH;

        modeBlend_ = std::min(1.0f, modeBlend_ + dt / kModeTransitionSec);
        float b = Smoothstep(modeBlend_);
        dispPos = modeFromPos_ * (1.0f - b) + modeToPos_ * b;
        dispQuat = NlerpQ(modeFromQuat_, modeToQuat_, b);
        dispW = modeFromW_ * (1.0f - b) + modeToW_ * b;
        dispH = modeFromH_ * (1.0f - b) + modeToH_ * b;
    }

    lastOutPos_ = dispPos;
    lastOutQuat_ = dispQuat;
    lastOutW_ = dispW;
    lastOutH_ = dispH;
    haveLastOut_ = true;
}

void ProjectionUI::Update(float dt, const XrInput& in, const XrInput::Pose& head,
                          const Vec3& dispPos, const Quat& dispQuat,
                          float dispW, float dispH, bool gazeOnDisplay,
                          float videoFps, bool streaming, bool touchDown,
                          float touchU, float touchV, float nowSec) {
    (void)nowSec;
    lastFps_ = videoFps;
    lastStreaming_ = streaming;
    touchDotOn_ = touchDown;
    touchDotU_ = touchU;
    touchDotV_ = touchV;
    pulseT_ += dt;

    // Control bar reveal: gaze dwell shows it, looking away hides it.
    if (gazeOnDisplay) {
        gazeDwellCtl_ += dt;
        hideT_ = 0.0f;
    } else {
        gazeDwellCtl_ = 0.0f;
        if (controlsVisible_) hideT_ += dt;
    }
    if (!controlsVisible_ && gazeDwellCtl_ >= kRevealDwellSec && head.valid) {
        controlsVisible_ = true;
        controlsT_ = 0.0f;
        ComputeControlBarPose(dispPos, dispQuat, dispW, dispH);
    }
    if (controlsVisible_) {
        controlsT_ += dt;
        if (hideT_ >= kHideDelaySec) {
            controlsVisible_ = false;
            hoverCtl_ = -1;
        } else {
            // Refresh the bar pose so it tracks the display.
            ComputeControlBarPose(dispPos, dispQuat, dispW, dispH);
        }
    }

    // Control bar input: right controller ray + trigger edge, or pinch.
    if (controlsVisible_ && barValid_) {
        const auto& aim = in.RightAim();
        float trig = in.TriggerValue();
        float pinch = in.PinchValue(true);
        bool trigEdge = trig > 0.5f && prevTrigger_ <= 0.5f;
        bool pinchEdge = pinch > 0.8f && prevPinchR_ <= 0.8f;
        prevTrigger_ = trig;
        prevPinchR_ = pinch;
        hoverCtl_ = -1;
        if (aim.valid) {
            Vec3 dir = aim.quat.rotate(Vec3(0, 0, -1));
            hoverCtl_ = (int)HitTestControls(aim.pos, dir);
            if (hoverCtl_ >= 0 && (trigEdge || pinchEdge)) {
                ActivateControl((CtlButton)hoverCtl_);
            }
        }
    } else {
        prevTrigger_ = in.TriggerValue();
        prevPinchR_ = in.PinchValue(true);
    }
}

bool ProjectionUI::ConsumesInput() const {
    // Swallow pointer input when the bar is up and the pointer is on it,
    // so a trigger pull on a button doesn't also tap the phone screen.
    return controlsVisible_ && hoverCtl_ >= 0;
}

void ProjectionUI::ComputeControlBarPose(const Vec3& dispPos,
                                         const Quat& dispQuat, float dispW,
                                         float dispH) {
    barW_ = kNumButtons * kBtnW + (kNumButtons - 1) * kBtnGap + 2 * kBarPad;
    barH_ = kBtnH + 2 * kBarPad;
    Vec3 down = dispQuat.rotate(Vec3(0, -1, 0));
    Vec3 out = dispQuat.rotate(Vec3(0, 0, 1));
    // Float just below the display, slightly in front.
    barPos_ = dispPos + down * (dispH * 0.5f + 0.030f + barH_ * 0.5f) +
              out * 0.004f;
    barQuat_ = dispQuat;
    barValid_ = true;
    (void)dispW;
}

ProjectionUI::CtlButton ProjectionUI::HitTestControls(const Vec3& origin,
                                                      const Vec3& dir) const {
    if (!barValid_) return CtlButton::None;
    float u, v;
    if (!RayQuadIntersect(origin, dir, barPos_, barQuat_, barW_, barH_, u,
                          v))
        return CtlButton::None;
    // Map UV to button index.
    float x = u * barW_ - kBarPad;  // into content area
    float step = kBtnW + kBtnGap;
    int idx = (int)(x / step);
    if (idx < 0 || idx >= kNumButtons) return CtlButton::None;
    float within = x - idx * step;
    if (within < 0 || within > kBtnW) return CtlButton::None;
    // v spans the full bar height; require the button band.
    float yTop = kBarPad + kBtnH;
    float y = (1.0f - v) * barH_;
    if (y < kBarPad || y > yTop) return CtlButton::None;
    return (CtlButton)idx;
}

const char* ProjectionUI::ControlLabel(CtlButton b) const {
    switch (b) {
        case CtlButton::Dim: return "Dimmer";
        case CtlButton::Brighten: return "Brighter";
        case CtlButton::Lock:
            return orientationLocked_ ? "Unlock" : "Lock";
        default: return "";
    }
}

void ProjectionUI::ActivateControl(CtlButton b) {
    switch (b) {
        case CtlButton::Dim:
            AdjustBrightness(-0.1f);
            break;
        case CtlButton::Brighten:
            AdjustBrightness(+0.1f);
            break;
        case CtlButton::Lock:
            // Lock at the bar's parent display pose (stored per-frame).
            SetOrientationLocked(!orientationLocked_, barPos_, barQuat_);
            break;
        default:
            break;
    }
}

void ProjectionUI::DrawPlate(const Mat4& viewProj, const Vec3& center,
                             const Quat& quat, float w, float h,
                             const float color[4], float cornerR) {
    Mat4 model = Mat4::FromPose(center, quat);
    Mat4 scale = Mat4::Identity();
    scale.m[0] = w;
    scale.m[5] = h;
    // UV [0,1] -> centered local: translate(-0.5,-0.5) then scale.
    Mat4 toCenter = Mat4::Identity();
    toCenter.m[12] = -0.5f * w;
    toCenter.m[13] = -0.5f * h;
    Mat4 mvp = viewProj * model * toCenter * scale;
    glUseProgram(plateProg_);
    glUniformMatrix4fv(plateUMvp_, 1, GL_FALSE, mvp.m);
    glUniform4fv(plateUColor_, 1, color);
    glUniform1f(plateUCorner_, cornerR);
    glUniform1f(plateUBorder_, 0.0f);
    float bc[4] = {0, 0, 0, 0};
    glUniform4fv(plateUBorderColor_, 1, bc);
    glUniform1f(plateUAspect_, w / h);
    glBindVertexArray(plateVao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glUseProgram(0);
}

void ProjectionUI::DrawRing(const Mat4& viewProj, const Vec3& center,
                            const Quat& quat, float w, float h,
                            const float color[4], float thickness) {
    // Ring = border-only plate slightly larger than the glass.
    Mat4 model = Mat4::FromPose(center, quat);
    Mat4 scale = Mat4::Identity();
    scale.m[0] = w;
    scale.m[5] = h;
    Mat4 toCenter = Mat4::Identity();
    toCenter.m[12] = -0.5f * w;
    toCenter.m[13] = -0.5f * h;
    Mat4 mvp = viewProj * model * toCenter * scale;
    glUseProgram(plateProg_);
    glUniformMatrix4fv(plateUMvp_, 1, GL_FALSE, mvp.m);
    float transparent[4] = {0, 0, 0, 0};
    glUniform4fv(plateUColor_, 1, transparent);
    glUniform1f(plateUCorner_, 0.06f);
    // Border width in UV of the min dimension (height here).
    glUniform1f(plateUBorder_, thickness / h);
    glUniform4fv(plateUBorderColor_, 1, color);
    glUniform1f(plateUAspect_, w / h);
    glBindVertexArray(plateVao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glUseProgram(0);
}

void ProjectionUI::DrawDot(const Mat4& viewProj, const Vec3& center,
                           const Quat& quat, float diameter,
                           const float color[4]) {
    // Circle = filled plate with corner radius 0.5.
    DrawPlate(viewProj, center, quat, diameter, diameter, color, 0.5f);
}

void ProjectionUI::DrawFrame(const Mat4& viewProj, const Vec3& dispPos,
                             const Quat& dispQuat, float dispW, float dispH,
                             float videoFps, bool streaming) {
    if (!ok_) return;

    // Depth-tested, non-writing, blended: the ring/dot sit just in front
    // of the glass, the shadow just behind it.
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    Vec3 out = dispQuat.rotate(Vec3(0, 0, 1));

    // 1. Soft drop shadow behind the panel — two stepped halo bands
    // (border-only, so they never overdraw the video even when no depth
    // was written, e.g. the fallback quad path).
    {
        Vec3 shadowPos = dispPos - out * 0.006f;
        float s1[4] = {kShadowCol[0], kShadowCol[1], kShadowCol[2], 0.16f};
        float s2[4] = {kShadowCol[0], kShadowCol[1], kShadowCol[2], 0.30f};
        DrawRing(viewProj, shadowPos, dispQuat, dispW * 1.09f, dispH * 1.09f,
                 s1, 0.025f);
        DrawRing(viewProj, shadowPos, dispQuat, dispW * 1.045f,
                 dispH * 1.045f, s2, 0.012f);
    }

    // 2. Warm-amber accent ring around the glass.
    {
        float ringAlpha;
        if (!streaming) {
            // Idle: faint trim.
            ringAlpha = 0.22f;
        } else if (videoFps < 1.0f) {
            // Connected but no frames yet: gentle breathing pulse.
            ringAlpha = 0.45f + 0.25f * sinf(pulseT_ * 3.0f);
        } else {
            ringAlpha = 0.85f;
        }
        float ringCol[4] = {kAmber[0], kAmber[1], kAmber[2], ringAlpha};
        Vec3 ringPos = dispPos + out * 0.0022f;
        DrawRing(viewProj, ringPos, dispQuat, dispW + 0.006f, dispH + 0.006f,
                 ringCol, 0.0028f);
    }

    // 3. Status dot: top-right corner, color = stream health.
    {
        const float* sc;
        if (!streaming) {
            sc = kStatusIdle;
        } else if (videoFps < 1.0f) {
            sc = kStatusDegraded;  // connecting / stalled
        } else if (videoFps < 20.0f) {
            sc = kStatusDegraded;
        } else {
            sc = kStatusGood;
        }
        Vec3 right = dispQuat.rotate(Vec3(1, 0, 0));
        Vec3 up = dispQuat.rotate(Vec3(0, 1, 0));
        Vec3 dotPos = dispPos + right * (dispW * 0.5f - 0.008f) +
                      up * (dispH * 0.5f - 0.008f) + out * 0.0028f;
        DrawDot(viewProj, dotPos, dispQuat, 0.009f, sc);
    }

    // 4. Touch dot: persistent amber dot while a finger is down.
    if (touchDotOn_) {
        Vec3 right = dispQuat.rotate(Vec3(1, 0, 0));
        Vec3 up = dispQuat.rotate(Vec3(0, 1, 0));
        Vec3 p = dispPos + right * ((touchDotU_ - 0.5f) * dispW) +
                 up * ((0.5f - touchDotV_) * dispH) + out * 0.0032f;
        float dotCol[4] = {kAmber[0], kAmber[1], kAmber[2], 0.9f};
        DrawDot(viewProj, p, dispQuat, 0.011f, dotCol);
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
}

void ProjectionUI::DrawControls(const Mat4& viewProj, const Vec3& headPos) {
    (void)headPos;
    if (!ok_ || !controlsVisible_ || !barValid_) return;

    float fade = Clamp01(controlsT_ / kFadeSec);
    if (hideT_ > 0.0f) {
        // Fading out during the hide delay tail.
        fade = Clamp01(1.0f - (hideT_ - (kHideDelaySec - kFadeSec)) / kFadeSec);
        if (fade <= 0.0f) return;
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    // Controls float above everything (no depth test — they're UI).
    glDisable(GL_DEPTH_TEST);

    // Bar background.
    float bgCol[4] = {kCharcoal[0], kCharcoal[1], kCharcoal[2],
                      kCharcoal[3] * fade};
    DrawPlate(viewProj, barPos_, barQuat_, barW_, barH_, bgCol, 0.18f);

    // Buttons.
    for (int i = 0; i < kNumButtons; ++i) {
        float cx = -barW_ * 0.5f + kBarPad + kBtnW * 0.5f +
                   i * (kBtnW + kBtnGap);
        Vec3 right = barQuat_.rotate(Vec3(1, 0, 0));
        Vec3 out = barQuat_.rotate(Vec3(0, 0, 1));
        Vec3 bp = barPos_ + right * cx + out * 0.001f;
        bool hov = (hoverCtl_ == i);
        float bcol[4];
        if (hov) {
            bcol[0] = kAmber[0];
            bcol[1] = kAmber[1];
            bcol[2] = kAmber[2];
            bcol[3] = 0.30f * fade;
        } else {
            bcol[0] = 0.16f;
            bcol[1] = 0.17f;
            bcol[2] = 0.19f;
            bcol[3] = 0.95f * fade;
        }
        DrawPlate(viewProj, bp, barQuat_, kBtnW, kBtnH, bcol, 0.22f);

        // Label.
        labelOverlay_.Clear();
        labelOverlay_.Printf("%s", ControlLabel((CtlButton)i));
        Mat4 lm = Mat4::FromPose(bp + out * 0.001f, barQuat_);
        labelOverlay_.Draw(viewProj, lm, kBtnW * 0.92f);
    }

    glDisable(GL_BLEND);
}

}  // namespace xrwrist
