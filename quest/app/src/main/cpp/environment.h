#pragma once
// v0.6.0: Comfortable VR environment for the XR Wrist Display.
//
// A void-black background is fatiguing and disorienting. This renderer
// provides a soft, warm, professional space the wrist display lives in:
//   - gradient sky dome (warm horizon -> deep blue-charcoal zenith)
//   - subtle radial ground grid with a soft platform glow under the user
//   - slow-drifting ambient motes for depth perception
//
// Everything is procedural (no textures), three draw calls total.
// Skipped entirely in passthrough mode — the real world is the backdrop.
//
// Human-centered: the horizon sits at eye level, nothing demands attention,
// and the palette is warm rather than sterile.

#include <GLES3/gl3.h>

#include "gl_render.h"

namespace xrwrist {

class EnvironmentRenderer {
public:
    EnvironmentRenderer() = default;
    ~EnvironmentRenderer() { Shutdown(); }

    bool Init();
    void Shutdown();

    // Draws sky + ground + motes. viewProj: per-eye view-projection.
    // headPos: sky dome follows the head (translation only).
    // timeSec: drives mote drift (any monotonic clock).
    void Draw(const Mat4& viewProj, const Vec3& headPos, float timeSec);

    bool IsReady() const { return ok_; }

private:
    bool BuildSky();
    bool BuildGround();
    bool BuildMotes();

    GLuint skyProg_ = 0;
    GLuint groundProg_ = 0;
    GLuint moteProg_ = 0;
    GLuint skyVao_ = 0, skyVbo_ = 0, skyIbo_ = 0;
    GLuint groundVao_ = 0, groundVbo_ = 0;
    GLuint moteVao_ = 0, moteVbo_ = 0;
    GLsizei skyIndexCount_ = 0;
    GLsizei moteCount_ = 0;

    GLint skyUMvp_ = -1;
    GLint groundUMvp_ = -1;
    GLint moteUMvp_ = -1;
    GLint moteUTime_ = -1;

    bool ok_ = false;
};

}  // namespace xrwrist
