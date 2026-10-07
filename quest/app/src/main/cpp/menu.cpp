// v0.6.0: In-VR menu system — floating Soft-Tech panel, Y-tap toggled,
// controller ray + hand poke navigable.
#include "menu.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "config.h"
#include "log.h"
#include "notify.h"

namespace xrwrist {
namespace {

// Soft-Tech palette (linear-ish, no pure white/black).
const float kColPanel[4]   = {0.075f, 0.082f, 0.105f, 0.94f};
const float kColRow[4]     = {0.135f, 0.148f, 0.185f, 0.96f};
const float kColRowHover[4] = {0.210f, 0.190f, 0.150f, 0.98f};
const float kColAmber[4]   = {0.910f, 0.620f, 0.265f, 1.0f};

// Conjugate of a unit quaternion (inverse rotation).
Quat QuatConj(const Quat& q) { return Quat(-q.x, -q.y, -q.z, q.w); }

GLuint CompileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        LOGE("menu: shader compile failed: %s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

GLuint LinkProgram(const char* vs, const char* fs) {
    GLuint v = CompileShader(GL_VERTEX_SHADER, vs);
    GLuint f = CompileShader(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        LOGE("menu: program link failed");
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

// Rounded-rect plate with border, in panel-local [0,1]x[0,1] UV space.
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
uniform float uBorder;   // border width in UV units
uniform vec4 uBorderColor;
uniform float uAspect;   // width / height
out vec4 oColor;
float sdRoundBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}
void main() {
    // Work in aspect-corrected space so corners are circular.
    vec2 p = vec2(vUv.x * uAspect, vUv.y);
    vec2 b = vec2(uAspect * 0.5, 0.5);
    float d = sdRoundBox(p - vec2(uAspect * 0.5, 0.5), b - uCorner, uCorner);
    float alpha = 1.0 - smoothstep(-0.004, 0.004, d);
    if (alpha < 0.01) discard;
    float border = 1.0 - smoothstep(uBorder * 0.5, uBorder * 0.5 + 0.004, abs(d));
    vec3 col = mix(uColor.rgb, uBorderColor.rgb, border * uBorderColor.a);
    oColor = vec4(col, uColor.a * alpha);
}
)";

const char* kRayVert = R"(
#version 300 es
layout(location=0) in vec3 aPos;
uniform mat4 uMvp;
void main() { gl_Position = uMvp * vec4(aPos, 1.0); }
)";

const char* kRayFrag = R"(
#version 300 es
precision mediump float;
uniform vec4 uColor;
out vec4 oColor;
void main() { oColor = uColor; }
)";

}  // namespace

const char* PageTitleStr(int page) {
    switch (page) {
        case 1: return "SETTINGS";
        case 2: return "ABOUT";
        default: return "XR WRIST";
    }
}

bool VrMenu::Init(const MenuActions& actions) {
    actions_ = actions;

    plateProg_ = LinkProgram(kPlateVert, kPlateFrag);
    rayProg_ = LinkProgram(kRayVert, kRayFrag);
    if (!plateProg_ || !rayProg_) {
        LOGE("menu: shader init failed");
        return false;
    }
    plateUMvp_ = glGetUniformLocation(plateProg_, "uMvp");
    plateUColor_ = glGetUniformLocation(plateProg_, "uColor");
    plateUCorner_ = glGetUniformLocation(plateProg_, "uCorner");
    plateUBorder_ = glGetUniformLocation(plateProg_, "uBorder");
    plateUBorderColor_ = glGetUniformLocation(plateProg_, "uBorderColor");
    plateUAspect_ = glGetUniformLocation(plateProg_, "uAspect");
    rayUMvp_ = glGetUniformLocation(rayProg_, "uMvp");
    rayUColor_ = glGetUniformLocation(rayProg_, "uColor");

    // Unit quad [0,1]x[0,1].
    {
        float q[] = {0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1};
        glGenVertexArrays(1, &plateVao_);
        glGenBuffers(1, &plateVbo_);
        glBindVertexArray(plateVao_);
        glBindBuffer(GL_ARRAY_BUFFER, plateVbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(q), q, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (void*)0);
        glBindVertexArray(0);
    }
    // Aim ray: 2-vertex line, positions updated per frame.
    {
        glGenVertexArrays(1, &rayVao_);
        glGenBuffers(1, &rayVbo_);
        glBindVertexArray(rayVao_);
        glBindBuffer(GL_ARRAY_BUFFER, rayVbo_);
        glBufferData(GL_ARRAY_BUFFER, 6 * sizeof(float), nullptr,
                     GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
        glBindVertexArray(0);
    }

    if (!headerOverlay_.Init() || !footerOverlay_.Init()) {
        LOGE("menu: text overlay init failed");
        return false;
    }
    headerOverlay_.SetBackgroundEnabled(false);
    footerOverlay_.SetBackgroundEnabled(false);

    RebuildRows();
    ok_ = true;
    LOGI("menu: init ok");
    return true;
}

void VrMenu::Shutdown() {
    for (GLuint p : {plateProg_, rayProg_})
        if (p) glDeleteProgram(p);
    for (GLuint v : {plateVao_, rayVao_})
        if (v) glDeleteVertexArrays(1, &v);
    for (GLuint b : {plateVbo_, rayVbo_})
        if (b) glDeleteBuffers(1, &b);
    plateProg_ = rayProg_ = 0;
    plateVao_ = rayVao_ = plateVbo_ = rayVbo_ = 0;
    rowOverlays_.clear();
    ok_ = false;
    open_ = false;
}

void VrMenu::SetOpen(bool open) {
    if (open == open_) return;
    open_ = open;
    if (open_) {
        openT_ = 0.0f;
        hover_ = -1;
        page_ = Page::Main;
        RebuildRows();
        LOGI("menu: opened");
    } else {
        LOGI("menu: closed");
    }
}

bool VrMenu::ConsumesInput() const {
    return open_ && (hover_ >= 0 || pressT_ < 0.25f);
}

float VrMenu::PanelHeight() const {
    float rowsH = rows_.empty()
                      ? 0.0f
                      : rows_.size() * RowHeight() +
                            (rows_.size() - 1) * RowGap();
    return kPad + kHeaderH + kPad * 0.5f + rowsH + kPad * 0.5f + kFooterH +
           kPad;
}

float VrMenu::RowTopY(size_t i) const {
    // Panel-local y (up positive, origin center) of the row's top edge.
    float top = PanelHeight() / 2.0f - kPad - kHeaderH - kPad * 0.5f;
    return top - (float)i * (RowHeight() + RowGap());
}

std::string VrMenu::RowLabel(size_t i) const {
    if (i >= rows_.size()) return "";
    const Row& r = rows_[i];
    return r.dynLabel ? r.dynLabel() : r.label;
}

void VrMenu::RebuildRows() {
    rows_.clear();
    auto& cfg = Config::Instance();
    const float kSizeStep = 0.02f;
    const float kDwellStep = 0.25f;

    if (page_ == Page::Main) {
        rows_.push_back(Row{
            "",
            [this]() {
                return std::string("Stream: ") +
                       (actions_.isStreaming ? (actions_.isStreaming()
                                                   ? "Stop"
                                                   : "Start")
                                            : "Start");
            },
            [this]() {
                if (actions_.toggleStream) actions_.toggleStream();
            }});
        rows_.push_back(Row{"Settings", {},
                            [this]() {
                                page_ = Page::Settings;
                                RebuildRows();
                            }});
        rows_.push_back(Row{
            "",
            [this]() {
                bool pt = actions_.isPassthrough
                              ? actions_.isPassthrough()
                              : false;
                return std::string("Environment: ") +
                       (pt ? "Passthrough" : "VR Room");
            },
            [this]() {
                if (actions_.togglePassthrough) actions_.togglePassthrough();
            }});
        rows_.push_back(Row{
            "",
            [this]() {
                return std::string("Projection: ") +
                       (actions_.projectionModeName
                            ? actions_.projectionModeName()
                            : "Wrist");
            },
            [this]() {
                if (actions_.cycleProjectionMode)
                    actions_.cycleProjectionMode();
            }});
        rows_.push_back(Row{"Tech info", {},
                            [this]() {
                                if (actions_.toggleDiagnostics)
                                    actions_.toggleDiagnostics();
                            }});
        rows_.push_back(Row{"About", {},
                            [this]() {
                                page_ = Page::About;
                                RebuildRows();
                            }});
    } else if (page_ == Page::Settings) {
        // v0.6.1: plain-language labels. Every row says what it does;
        // current values shown in familiar units (cm, seconds).
        rows_.push_back(Row{
            "", [&cfg]() {
                char b[64];
                snprintf(b, sizeof(b), "Bigger display (now %.0f cm)",
                         cfg.GetFloat(ConfigKey::EngagedSizeM) * 100.0f);
                return std::string(b);
            },
            [&cfg, kSizeStep]() {
                float v = cfg.GetFloat(ConfigKey::EngagedSizeM) + kSizeStep;
                v = std::min(0.60f, std::max(0.30f, v));
                cfg.SetFloat(ConfigKey::EngagedSizeM, v);
                char b[64];
                snprintf(b, sizeof(b), "Display size: %.0f cm", v * 100.0f);
                Notify::Instance().Info(b);
            }});
        rows_.push_back(Row{
            "", [&cfg]() {
                char b[64];
                snprintf(b, sizeof(b), "Smaller display (now %.0f cm)",
                         cfg.GetFloat(ConfigKey::EngagedSizeM) * 100.0f);
                return std::string(b);
            },
            [&cfg, kSizeStep]() {
                float v = cfg.GetFloat(ConfigKey::EngagedSizeM) - kSizeStep;
                v = std::min(0.60f, std::max(0.30f, v));
                cfg.SetFloat(ConfigKey::EngagedSizeM, v);
                char b[64];
                snprintf(b, sizeof(b), "Display size: %.0f cm", v * 100.0f);
                Notify::Instance().Info(b);
            }});
        rows_.push_back(Row{
            "", [&cfg]() {
                char b[64];
                snprintf(b, sizeof(b), "Look longer to open (now %.1fs)",
                         cfg.GetFloat(ConfigKey::GazeDwellSec));
                return std::string(b);
            },
            [&cfg, kDwellStep]() {
                float v = cfg.GetFloat(ConfigKey::GazeDwellSec) + kDwellStep;
                v = std::min(3.0f, std::max(0.5f, v));
                cfg.SetFloat(ConfigKey::GazeDwellSec, v);
                char b[64];
                snprintf(b, sizeof(b), "Look time: %.1f seconds", v);
                Notify::Instance().Info(b);
            }});
        rows_.push_back(Row{
            "", [&cfg]() {
                char b[64];
                snprintf(b, sizeof(b), "Look quicker to open (now %.1fs)",
                         cfg.GetFloat(ConfigKey::GazeDwellSec));
                return std::string(b);
            },
            [&cfg, kDwellStep]() {
                float v = cfg.GetFloat(ConfigKey::GazeDwellSec) - kDwellStep;
                v = std::min(3.0f, std::max(0.5f, v));
                cfg.SetFloat(ConfigKey::GazeDwellSec, v);
                char b[64];
                snprintf(b, sizeof(b), "Look time: %.1f seconds", v);
                Notify::Instance().Info(b);
            }});
        rows_.push_back(Row{
            "", [&cfg]() {
                return std::string("Voice commands: ") +
                       (cfg.GetBool(ConfigKey::VoiceEnabled) ? "On" : "Off");
            },
            [&cfg]() {
                cfg.SetBool(ConfigKey::VoiceEnabled,
                            !cfg.GetBool(ConfigKey::VoiceEnabled));
            }});
        rows_.push_back(Row{
            "", [&cfg]() {
                return std::string("Auto-move display: ") +
                       (cfg.GetBool(ConfigKey::AutoTransition) ? "On" : "Off");
            },
            [&cfg]() {
                cfg.SetBool(ConfigKey::AutoTransition,
                            !cfg.GetBool(ConfigKey::AutoTransition));
            }});
        rows_.push_back(Row{
            "", [&cfg]() {
                return std::string("Hand avatars: ") +
                       (cfg.GetBool(ConfigKey::AvatarEnabled) ? "On" : "Off");
            },
            [&cfg]() {
                cfg.SetBool(ConfigKey::AvatarEnabled,
                            !cfg.GetBool(ConfigKey::AvatarEnabled));
            }});
        rows_.push_back(Row{"< Back", {},
                            [this]() {
                                page_ = Page::Main;
                                RebuildRows();
                            }});
    } else {  // About
        std::string ver =
            actions_.versionString ? actions_.versionString() : "v0.6.0";
        rows_.push_back(
            Row{"XR Wrist Display " + ver, {}, {}, false});
        rows_.push_back(
            Row{"Built with OpenXR", {}, {}, false});
        rows_.push_back(Row{"github.com/Phantom31495/", {}, {}, false});
        rows_.push_back(Row{"xr-wrist-display", {}, {}, false});
        rows_.push_back(Row{"< Back", {},
                            [this]() {
                                page_ = Page::Main;
                                RebuildRows();
                            }});
    }

    // Rebuild row text overlays.
    rowOverlays_.clear();
    rowOverlays_.reserve(rows_.size());
    for (size_t i = 0; i < rows_.size(); ++i) {
        auto ov = std::make_unique<devtools::TextOverlay>();
        if (ov->Init()) {
            ov->SetBackgroundEnabled(false);
            rowOverlays_.push_back(std::move(ov));
        } else {
            rowOverlays_.push_back(nullptr);
        }
    }
    // Refresh all labels now.
    for (size_t i = 0; i < rows_.size(); ++i) {
        if (rowOverlays_[i]) {
            rowOverlays_[i]->Clear();
            rowOverlays_[i]->Printf("%s", RowLabel(i).c_str());
        }
    }
    headerOverlay_.Clear();
    headerOverlay_.Printf("%s", PageTitleStr((int)page_));
    footerOverlay_.Clear();
    footerOverlay_.Printf("Y closes - Trigger or pinch selects");
}

void VrMenu::ComputePanelPose(const XrInput::Pose& head) {
    // Comfortable placement: 0.55m ahead, slightly below eye line,
    // billboarded to face the user. No neck strain.
    Vec3 fwd = head.quat.rotate(Vec3(0, 0, -1));
    Vec3 up = head.quat.rotate(Vec3(0, 1, 0));
    panelPos_ = head.pos + fwd * 0.55f - up * 0.06f;
    // Full billboard via the existing helper (defined in xr_core.cpp).
    // We reimplement the yaw-preserving variant here to keep menu.cpp
    // self-contained: face the head position directly.
    Vec3 zAxis = (head.pos - panelPos_).normalized();
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
    panelQuat_ = q;
    panelValid_ = true;
}

int VrMenu::UpdateHover(const XrInput& in) {
    if (!panelValid_) return -1;
    float pw = PanelWidth();
    float ph = PanelHeight();

    // 1) Controller ray (primary).
    const auto& aim = in.RightAim();
    if (aim.valid) {
        Vec3 dir = aim.quat.rotate(Vec3(0, 0, -1));
        float u, v;
        if (RayQuadIntersect(aim.pos, dir, panelPos_, panelQuat_, pw, ph,
                             u, v)) {
            rayOrigin_ = aim.pos;
            rayDir_ = dir;
            rayLen_ = (panelPos_ - aim.pos).length();
            rayValid_ = true;
            // v=0 at top. Row i spans v in [rowTopV_i, rowBotV_i].
            for (size_t i = 0; i < rows_.size(); ++i) {
                float topY = RowTopY(i);
                float vTop = 0.5f - topY / ph;
                float vBot = vTop + RowHeight() / ph;
                if (v >= vTop && v <= vBot && rows_[i].selectable)
                    return (int)i;
            }
            return -1;  // on panel but not on a row
        }
    }
    rayValid_ = false;

    // 2) Hand poke (either hand) — direct touch.
    for (int h = 0; h < 2; ++h) {
        bool right = (h == 1);
        auto poke = in.PokePose(right);
        bool tracked = in.HandInteractionAvailable()
                           ? true
                           : in.HandTracked(right);
        if (!tracked || !poke.valid) continue;
        // Panel-local: translate then unrotate.
        Vec3 d = poke.pos - panelPos_;
        Vec3 local = QuatConj(panelQuat_).rotate(d);
        float touchDist = Config::Instance().GetFloat(ConfigKey::TouchDistanceM);
        if (fabsf(local.z) > touchDist * 2.0f) continue;
        if (fabsf(local.x) > pw / 2.0f || fabsf(local.y) > ph / 2.0f)
            continue;
        for (size_t i = 0; i < rows_.size(); ++i) {
            float topY = RowTopY(i);
            float botY = topY - RowHeight();
            if (local.y <= topY && local.y >= botY &&
                rows_[i].selectable)
                return (int)i;
        }
    }
    return -1;
}

void VrMenu::Activate(int row) {
    if (row < 0 || (size_t)row >= rows_.size()) return;
    const Row& r = rows_[(size_t)row];
    if (!r.selectable || !r.onActivate) return;
    pressed_ = row;
    pressT_ = 0.0f;
    r.onActivate();
    // Dynamic labels may have changed (e.g. "Stream: Stop").
    for (size_t i = 0; i < rows_.size(); ++i) {
        if (rowOverlays_[i]) {
            rowOverlays_[i]->Clear();
            rowOverlays_[i]->Printf("%s", RowLabel(i).c_str());
        }
    }
}

void VrMenu::Update(float dt, const XrInput& in, const XrInput::Pose& head,
                    float nowSec) {
    // Y quick-tap (<0.45s) toggles the menu. Y-hold (>=0.8s) remains
    // voice push-to-talk (handled by XrApp::UpdateVoice).
    bool yDown = in.YDown();
    if (yDown && !prevYDown_) yPressT_ = nowSec;
    if (!yDown && prevYDown_ && (nowSec - yPressT_) < 0.45f) Toggle();
    prevYDown_ = yDown;

    pressT_ += dt;
    if (!open_) return;
    openT_ += dt;
    if (!head.valid) return;

    ComputePanelPose(head);
    hover_ = UpdateHover(in);

    // Trigger edge (right controller).
    float trig = in.TriggerValue();
    bool trigEdge = trig > 0.55f && prevTrigger_ <= 0.55f;
    prevTrigger_ = trig;
    // Pinch edge (either hand).
    float pinchR = in.PinchValue(true);
    float pinchL = in.PinchValue(false);
    bool pinchEdge = (pinchR > 0.8f && prevPinchR_ <= 0.8f) ||
                     (pinchL > 0.8f && prevPinchL_ <= 0.8f);
    prevPinchR_ = pinchR;
    prevPinchL_ = pinchL;

    if ((trigEdge || pinchEdge) && hover_ >= 0) Activate(hover_);
}

void VrMenu::DrawPlate(const Mat4& viewProj, const Vec3& center,
                       const Quat& quat, float w, float h,
                       const float color[4], float cornerR, float borderW,
                       const float borderColor[4]) {
    // Plate local: [0,1]x[0,1] -> centered, w x h meters.
    Mat4 model = Mat4::FromPose(center, quat);
    Mat4 anchor = Mat4::Identity();
    anchor.m[12] = -0.5f;
    anchor.m[13] = -0.5f;
    Mat4 scale = Mat4::Identity();
    scale.m[0] = w;
    scale.m[5] = h;
    Mat4 mvp = viewProj * model * scale * anchor;
    glUseProgram(plateProg_);
    glUniformMatrix4fv(plateUMvp_, 1, GL_FALSE, mvp.m);
    glUniform4f(plateUColor_, color[0], color[1], color[2], color[3]);
    glUniform1f(plateUCorner_, cornerR);
    glUniform1f(plateUBorder_, borderW);
    glUniform4f(plateUBorderColor_, borderColor[0], borderColor[1],
                borderColor[2], borderColor[3]);
    glUniform1f(plateUAspect_, w / h);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(plateVao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glDisable(GL_BLEND);
}

void VrMenu::DrawAimRay(const Mat4& viewProj, const Vec3& origin,
                        const Vec3& dir, float length) {
    float verts[6] = {origin.x, origin.y, origin.z,
                      origin.x + dir.x * length,
                      origin.y + dir.y * length,
                      origin.z + dir.z * length};
    glBindBuffer(GL_ARRAY_BUFFER, rayVbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(rayProg_);
    glUniformMatrix4fv(rayUMvp_, 1, GL_FALSE, viewProj.m);
    glUniform4f(rayUColor_, 0.91f, 0.62f, 0.27f, 0.45f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(rayVao_);
    glDrawArrays(GL_LINES, 0, 2);
    glBindVertexArray(0);
    glDisable(GL_BLEND);
}

void VrMenu::Draw(const Mat4& viewProj, const Vec3& headPos) {
    (void)headPos;
    if (!ok_ || !open_ || !panelValid_) return;

    // Open animation: ease-out scale 0.92 -> 1.0 over 0.18s.
    float t = std::min(openT_ / 0.18f, 1.0f);
    float ease = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
    float scale = 0.92f + 0.08f * ease;

    float pw = PanelWidth() * scale;
    float ph = PanelHeight() * scale;

    // Backdrop plate.
    const float kNoBorder[4] = {0, 0, 0, 0};
    DrawPlate(viewProj, panelPos_, panelQuat_, pw, ph, kColPanel, 0.055f,
              0.0f, kNoBorder);

    // Header text (top, centered).
    {
        float headerY = ph / 2.0f - kPad * scale - kHeaderH * scale / 2.0f;
        Vec3 hp = panelPos_ + panelQuat_.rotate(Vec3(0, headerY, 0.002f));
        Mat4 model = Mat4::FromPose(hp, panelQuat_);
        headerOverlay_.Draw(viewProj, model, 0.20f * scale);
    }

    // Rows.
    float rowW = (PanelWidth() - 2.0f * kPad) * scale;
    for (size_t i = 0; i < rows_.size(); ++i) {
        float topY = RowTopY(i) * scale;
        float cy = topY - RowHeight() * scale / 2.0f;
        bool isHover = (int)i == hover_;
        bool isPressed = (int)i == pressed_ && pressT_ < 0.15f;
        float rs = isPressed ? 0.96f : 1.0f;
        const float* col = isHover ? kColRowHover : kColRow;
        const float* borderCol = isHover ? kColAmber : kNoBorder;
        Vec3 rp = panelPos_ + panelQuat_.rotate(Vec3(0, cy, 0.0015f));
        DrawPlate(viewProj, rp, panelQuat_, rowW * rs,
                  RowHeight() * scale * rs, col, 0.16f,
                  isHover ? 0.030f : 0.0f, borderCol);
        // Label.
        if (rowOverlays_[i]) {
            Vec3 tp = panelPos_ + panelQuat_.rotate(Vec3(0, cy, 0.003f));
            Mat4 model = Mat4::FromPose(tp, panelQuat_);
            // Text ~11mm cap height: glyph scale s.t. 8px = 0.011m.
            std::string label = RowLabel(i);
            float textW = (float)label.size() * 0.011f;
            rowOverlays_[i]->Draw(viewProj, model, textW);
        }
    }

    // Footer hint.
    {
        float footerY = -ph / 2.0f + kPad * scale + kFooterH * scale / 2.0f;
        Vec3 fp = panelPos_ + panelQuat_.rotate(Vec3(0, footerY, 0.002f));
        Mat4 model = Mat4::FromPose(fp, panelQuat_);
        footerOverlay_.Draw(viewProj, model, 0.24f * scale);
    }

    // Aim ray while hovering the panel.
    if (rayValid_) DrawAimRay(viewProj, rayOrigin_, rayDir_, rayLen_);
}

}  // namespace xrwrist
