#pragma once
// Premium procedural avatar: stylized forearm + fully articulated hand,
// with a smartwatch (metallic bezel + video glass) mounted on the wrist.
//
// Coordinate convention: everything is built in the LEFT controller's grip
// space — +X right, +Y up (back of hand), -Z forward (toward fingers).
// The forearm extends toward +Z (back toward the elbow).

#include <GLES3/gl3.h>

#include <vector>

#include "gl_render.h"
#include "input.h"

namespace xrwrist {

// ---------------------------------------------------------------------------
// Named proportions (meters). Tune here; no magic numbers in the code below.
// ---------------------------------------------------------------------------
namespace avatar_dims {

// Forearm: tapered limb from wrist (y=0) to elbow (y=+len) in limb space.
constexpr float kForearmLength = 0.26f;
constexpr float kWristRadius = 0.030f;
constexpr float kElbowRadius = 0.043f;
// Pitch of the limb in grip space: RotX maps limb +Y onto the elbow
// direction. 115 deg aims the elbow back (+Z) and down (-Y) — the natural
// direction when the wrist is raised to check the time.
constexpr float kForearmPitchDeg = 115.0f;
// Wrist joint position in grip space (grip origin sits inside the palm).
constexpr float kWristJointX = 0.0f;
constexpr float kWristJointY = 0.0f;
constexpr float kWristJointZ = 0.035f;

// Palm: superellipsoid half-extents + center in grip space.
constexpr float kPalmHalfX = 0.042f;
constexpr float kPalmHalfY = 0.020f;
constexpr float kPalmHalfZ = 0.055f;
constexpr float kPalmCenterX = 0.0f;
constexpr float kPalmCenterY = -0.004f;
constexpr float kPalmCenterZ = -0.018f;
// Palm roundness exponents (1 = ellipsoid, 0 = box). 0.55 reads as "hand".
constexpr float kPalmRound = 0.55f;

// Knuckle positions in palm-local space (front edge of the palm).
constexpr float kKnuckleY = 0.010f;
constexpr float kKnuckleZ = -0.050f;

// Finger table: knuckle X (palm-local), segment radius, phalanx lengths.
// Ordered index, middle, ring, pinky. Left hand: -X is the thumb side.
struct FingerSpec {
    float knuckleX;
    float radius;
    float segLen[3];  // proximal, middle, distal
};
constexpr FingerSpec kFingers[4] = {
    {-0.030f, 0.0085f, {0.028f, 0.022f, 0.018f}},  // index
    {-0.010f, 0.0090f, {0.030f, 0.024f, 0.020f}},  // middle
    {0.010f, 0.0085f, {0.028f, 0.023f, 0.019f}},   // ring
    {0.030f, 0.0075f, {0.022f, 0.018f, 0.015f}},   // pinky
};
// Curl distribution across the three joints at full flexion (radians).
constexpr float kCurlProximal = 0.95f;
constexpr float kCurlMiddle = 1.05f;
constexpr float kCurlDistal = 0.70f;

// Thumb: base position (palm-local), pointing direction, segment lengths.
constexpr float kThumbBaseX = -0.040f;
constexpr float kThumbBaseY = 0.002f;
constexpr float kThumbBaseZ = -0.010f;
constexpr float kThumbDirX = -0.50f;
constexpr float kThumbDirY = -0.35f;
constexpr float kThumbDirZ = -0.80f;
constexpr float kThumbRadius = 0.0095f;
constexpr float kThumbSegLen[2] = {0.028f, 0.024f};
constexpr float kThumbCurl = 0.80f;       // flexion per segment at full input
constexpr float kThumbAbductMax = 0.45f;  // radians of sideways motion

// Smartwatch.
constexpr float kWatchFaceW = 0.090f;
constexpr float kWatchFaceH = 0.160f;
constexpr float kBezelWidth = 0.007f;
constexpr float kBezelCornerR = 0.016f;
constexpr float kGlassCornerR = 0.012f;
// Watch center offset from the grip origin, in grip space.
constexpr float kWatchLiftY = 0.052f;
constexpr float kWatchLiftZ = 0.012f;
// Blend between "face the head" (1.0) and "face wrist-up" (0.0).
constexpr float kWatchHeadBias = 0.70f;

// Skin material.
constexpr float kSkinR = 0.72f;
constexpr float kSkinG = 0.52f;
constexpr float kSkinB = 0.40f;
constexpr float kSkinWrap = 0.45f;  // wrap-lighting amount (SSS approximation)

}  // namespace avatar_dims

// ---------------------------------------------------------------------------
// AvatarRenderer
// ---------------------------------------------------------------------------
class AvatarRenderer {
public:
    // Interleaved vertex: position, smooth normal, uv.
    struct Vert {
        float p[3];
        float n[3];
        float uv[2];
    };
    struct Mesh {
        GLuint vao = 0;
        GLuint vbo = 0;
        GLuint ibo = 0;
        GLsizei indexCount = 0;
        // GODMODE: precomputed unique-edge index buffer for wireframe view.
        GLuint wireIbo = 0;
        GLsizei wireIndexCount = 0;
    };

    // GODMODE debug visualization modes (driven by the future dev console).
    enum class DebugMode {
        Off = 0,      // premium shaded render
        Wireframe = 1,// unique-edge wireframe, flat emissive color
        Normals = 2,  // world-space normals visualized as RGB
    };
    void SetDebugMode(DebugMode mode) { debugMode_ = mode; }
    DebugMode GetDebugMode() const { return debugMode_; }

    // Per-frame render statistics for the stats overlay. Call once per frame
    // after all draws; counters reset on read.
    struct RenderStats {
        uint32_t drawCalls = 0;
        uint32_t triangles = 0;
    };
    RenderStats TakeRenderStats() {
        RenderStats s = stats_;
        stats_ = RenderStats{};
        return s;
    }

    AvatarRenderer() = default;
    ~AvatarRenderer() { Shutdown(); }

    bool Init();
    void Shutdown();

    // Articulation inputs, all normalized. Curls: 0 = straight, 1 = full fist.
    struct HandPose {
        float indexCurl = 0.0f;   // left trigger
        float gripCurl = 0.0f;    // left squeeze — applies to all fingers
        float thumbFlex = 0.0f;   // -1..1, thumbstick Y (up = extend)
        float thumbAbduct = 0.0f; // -1..1, thumbstick X
    };

    // Watch face pose for the current grip + head position. The face normal
    // blends head-facing (readability) with wrist-up (attached feel); the
    // face "up" points toward the fingers like a real watch.
    static void ComputeWatchPose(const XrInput::Pose& grip, const Vec3& headPos,
                                 Vec3& outPos, Quat& outQuat);

    // v0.4.0: dynamic sizing — uniform scale applied to the whole watch
    // (bezel + crown + glass) around the face center.
    void SetWatchScale(float s) { watchScale_ = s; }
    // v0.4.0: pre-detach warning glow on the bezel (0 = off, 1 = full).
    void SetWatchGlow(float g) { watchGlow_ = g; }
    // v0.4.0: direct-touch ripple on the glass (uv center + intensity).
    void SetTouchPulse(float u, float v, float i) {
        pulseU_ = u; pulseV_ = v; pulseI_ = i;
    }
    // v0.4.0: Meta-fluent floating panel (fixed mode): dark metal frame +
    // rounded video glass, sized w x h meters. Reuses the watch meshes.
    void DrawPanel(const Mat4& viewProj, const Vec3& camPos,
                   const Vec3& pos, const Quat& quat,
                   float w, float h, GLuint videoTex);

    // viewProj: projection * view for the current eye (world space).
    // mirror: true renders the right hand (mirrored across the grip's local
    // X axis — the geometry is authored for the left hand).
    void DrawHand(const Mat4& viewProj, const Vec3& camPos,
                  const XrInput::Pose& grip, const HandPose& hand, bool mirror);
    void DrawWatch(const Mat4& viewProj, const Vec3& camPos,
                   const Vec3& watchPos, const Quat& watchQuat,
                   GLuint videoTex);

private:
    struct Program {
        GLuint prog = 0;
        GLint uMvp = -1;
        GLint uModel = -1;
        GLint uCamPos = -1;
        // skin
        GLint uSkinColor = -1;
        GLint uLightDir = -1;
        GLint uWrap = -1;
        // glass
        GLint uTex = -1;
        GLint uTexMat = -1;
        GLint uCornerR = -1;  // rounded-corner radius in UV units
        GLint uAspect = -1;   // face width / height
        // v0.4.0: touch ripple
        GLint uPulse = -1;    // vec2: ripple center in UV
        GLint uPulseI = -1;   // ripple intensity 0..1
        // v0.4.0: metal glow (pre-detach warning)
        GLint uGlow = -1;
        GLint uGlowColor = -1;
    };

    bool BuildPrograms();
    bool BuildMeshes();
    GLuint CompileProgram(const char* vsSrc, const char* fsSrc);
    void DrawMesh(const Mesh& mesh, Program& p, const Mat4& mvp,
                  const Mat4& model);

    Program skinProg_;
    Program metalProg_;
    Program glassProg_;
    Program debugProg_;  // GODMODE: wireframe / normals visualization
    // debugProg_ extra uniforms
    GLint uDebugKind_ = -1;   // 0 = flat wireframe color, 1 = normals
    GLint uDebugColor_ = -1;

    Mesh forearm_;
    Mesh palm_;
    Mesh fingerSeg_[4][3];
    Mesh thumbSeg_[2];
    Mesh bezel_;
    Mesh glass_;
    Mesh crown_;

    DebugMode debugMode_ = DebugMode::Off;
    RenderStats stats_;

    // v0.4.0 state (driven per-frame from XrApp).
    float watchScale_ = 1.0f;
    float watchGlow_ = 0.0f;
    float pulseU_ = 0.5f, pulseV_ = 0.5f, pulseI_ = 0.0f;

    bool ok_ = false;
};

}  // namespace xrwrist
