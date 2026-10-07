// v0.6.0: Comfortable VR environment — gradient sky, radial ground grid,
// ambient motes. All procedural, three draw calls.
#include "environment.h"

#include <cmath>
#include <cstdlib>
#include <vector>

#include "log.h"

namespace xrwrist {
namespace {

// --- shader helpers (same pattern as devtools.cpp) ---
GLuint CompileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        LOGE("environment: shader compile failed: %s", log);
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
        LOGE("environment: program link failed");
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

// --- sky dome: vertical gradient, warm horizon, soft sun glow ---
const char* kSkyVert = R"(
#version 300 es
layout(location=0) in vec3 aPos;
uniform mat4 uMvp;
out vec3 vDir;
void main() {
    vDir = normalize(aPos);
    vec4 p = uMvp * vec4(aPos, 1.0);
    // Push to far plane so the dome never clips.
    gl_Position = p.xyww;
}
)";

const char* kSkyFrag = R"(
#version 300 es
precision mediump float;
in vec3 vDir;
out vec4 oColor;
// Soft-Tech palette: warm amber horizon, deep blue-charcoal zenith.
void main() {
    float h = clamp(vDir.y, -1.0, 1.0);
    vec3 zenith  = vec3(0.055, 0.075, 0.115);
    vec3 mid     = vec3(0.135, 0.155, 0.210);
    vec3 horizon = vec3(0.415, 0.285, 0.165);
    vec3 below   = vec3(0.075, 0.070, 0.085);
    vec3 col;
    if (h >= 0.0) {
        float t1 = smoothstep(0.0, 0.28, h);
        float t2 = smoothstep(0.20, 0.85, h);
        col = mix(horizon, mid, t1);
        col = mix(col, zenith, t2);
    } else {
        col = mix(horizon, below, smoothstep(0.0, -0.35, h));
    }
    // Soft warm glow toward a fixed "sun" azimuth for depth cueing.
    vec3 sunDir = normalize(vec3(0.45, 0.28, -0.85));
    float sun = pow(max(dot(normalize(vDir), sunDir), 0.0), 24.0);
    col += vec3(0.55, 0.34, 0.14) * sun * 0.55;
    oColor = vec4(col, 1.0);
}
)";

// --- ground: radial grid fading with distance + platform glow ---
const char* kGroundVert = R"(
#version 300 es
layout(location=0) in vec3 aPos;  // xz plane, y=0, centered at origin
uniform mat4 uMvp;
out vec2 vXz;
void main() {
    vXz = aPos.xz;
    gl_Position = uMvp * vec4(aPos, 1.0);
}
)";

const char* kGroundFrag = R"(
#version 300 es
precision mediump float;
in vec2 vXz;
out vec4 oColor;
void main() {
    float r = length(vXz);
    // Fade everything out by ~14m so the disc edge never shows.
    float fade = exp(-r * 0.30);
    // Concentric rings every 1m.
    float ring = smoothstep(0.94, 1.0, 1.0 - abs(fract(r) - 0.5) * 2.0);
    // 24 radial spokes.
    float ang = atan(vXz.y, vXz.x) / 6.2831853 + 0.5;
    float spoke = smoothstep(0.985, 1.0, 1.0 - abs(fract(ang * 24.0) - 0.5) * 2.0);
    float grid = max(ring * 0.8, spoke * 0.55) * fade;
    // Soft platform glow under the user.
    float glow = exp(-r * r * 0.55);
    vec3 base = vec3(0.070, 0.075, 0.095);
    vec3 line = vec3(0.85, 0.60, 0.30);
    vec3 col = base + line * grid * 0.35 + line * glow * 0.10;
    float alpha = clamp(0.92 * fade + glow * 0.35, 0.0, 1.0);
    oColor = vec4(col, alpha);
}
)";

// --- motes: slow-drifting points for depth perception ---
const char* kMoteVert = R"(
#version 300 es
layout(location=0) in vec3 aPos;
layout(location=1) in float aSeed;
uniform mat4 uMvp;
uniform float uTime;
out float vAlpha;
void main() {
    vec3 p = aPos;
    p.x += sin(uTime * 0.10 + aSeed * 6.2831) * 0.35;
    p.y += sin(uTime * 0.07 + aSeed * 12.566) * 0.25;
    p.z += cos(uTime * 0.09 + aSeed * 9.4248) * 0.35;
    vec4 mv = uMvp * vec4(p, 1.0);
    gl_Position = mv;
    float dist = max(-(uMvp * vec4(p, 1.0)).z, 0.1);
    gl_PointSize = clamp(90.0 / dist, 1.5, 7.0);
    vAlpha = 0.30 * exp(-dist * 0.12);
}
)";

const char* kMoteFrag = R"(
#version 300 es
precision mediump float;
in float vAlpha;
out vec4 oColor;
void main() {
    vec2 c = gl_PointCoord - vec2(0.5);
    float d = smoothstep(0.5, 0.12, length(c));
    oColor = vec4(1.0, 0.85, 0.62, vAlpha * d);
}
)";

}  // namespace

bool EnvironmentRenderer::Init() {
    skyProg_ = LinkProgram(kSkyVert, kSkyFrag);
    groundProg_ = LinkProgram(kGroundVert, kGroundFrag);
    moteProg_ = LinkProgram(kMoteVert, kMoteFrag);
    if (!skyProg_ || !groundProg_ || !moteProg_) {
        LOGE("environment: shader init failed");
        return false;
    }
    skyUMvp_ = glGetUniformLocation(skyProg_, "uMvp");
    groundUMvp_ = glGetUniformLocation(groundProg_, "uMvp");
    moteUMvp_ = glGetUniformLocation(moteProg_, "uMvp");
    moteUTime_ = glGetUniformLocation(moteProg_, "uTime");

    if (!BuildSky() || !BuildGround() || !BuildMotes()) return false;
    ok_ = true;
    LOGI("environment: init ok");
    return true;
}

void EnvironmentRenderer::Shutdown() {
    for (GLuint p : {skyProg_, groundProg_, moteProg_})
        if (p) glDeleteProgram(p);
    for (GLuint v : {skyVao_, groundVao_, moteVao_})
        if (v) glDeleteVertexArrays(1, &v);
    for (GLuint b : {skyVbo_, skyIbo_, groundVbo_, moteVbo_})
        if (b) glDeleteBuffers(1, &b);
    skyProg_ = groundProg_ = moteProg_ = 0;
    skyVao_ = groundVao_ = moteVao_ = 0;
    skyVbo_ = skyIbo_ = groundVbo_ = moteVbo_ = 0;
    ok_ = false;
}

bool EnvironmentRenderer::BuildSky() {
    // UV sphere, radius 40, inward-facing (we see the inside).
    const int latSegs = 24, lonSegs = 48;
    std::vector<float> verts;
    std::vector<uint16_t> idx;
    for (int lat = 0; lat <= latSegs; ++lat) {
        float theta = (float)lat / latSegs * (float)M_PI;
        float y = cosf(theta);
        float r = sinf(theta);
        for (int lon = 0; lon <= lonSegs; ++lon) {
            float phi = (float)lon / lonSegs * 2.0f * (float)M_PI;
            verts.push_back(40.0f * r * cosf(phi));
            verts.push_back(40.0f * y);
            verts.push_back(40.0f * r * sinf(phi));
        }
    }
    for (int lat = 0; lat < latSegs; ++lat) {
        for (int lon = 0; lon < lonSegs; ++lon) {
            uint16_t a = (uint16_t)(lat * (lonSegs + 1) + lon);
            uint16_t b = (uint16_t)(a + lonSegs + 1);
            // Winding reversed for inside view.
            idx.push_back(a);
            idx.push_back(a + 1);
            idx.push_back(b);
            idx.push_back(a + 1);
            idx.push_back(b + 1);
            idx.push_back(b);
        }
    }
    skyIndexCount_ = (GLsizei)idx.size();
    glGenVertexArrays(1, &skyVao_);
    glGenBuffers(1, &skyVbo_);
    glGenBuffers(1, &skyIbo_);
    glBindVertexArray(skyVao_);
    glBindBuffer(GL_ARRAY_BUFFER, skyVbo_);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float),
                 verts.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, skyIbo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(uint16_t),
                 idx.data(), GL_STATIC_DRAW);
    glBindVertexArray(0);
    return true;
}

bool EnvironmentRenderer::BuildGround() {
    // Single large triangle-fan-ish quad: just two triangles covering
    // a 30m disc approximated by a quad; the shader does the radial work.
    // Use a triangle strip quad [-16,16]^2.
    float q[] = {-16, 0, -16,  16, 0, -16,  -16, 0, 16,  16, 0, 16};
    glGenVertexArrays(1, &groundVao_);
    glGenBuffers(1, &groundVbo_);
    glBindVertexArray(groundVao_);
    glBindBuffer(GL_ARRAY_BUFFER, groundVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(q), q, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
    glBindVertexArray(0);
    return true;
}

bool EnvironmentRenderer::BuildMotes() {
    const int kMotes = 140;
    std::vector<float> data;
    data.reserve(kMotes * 4);
    // Deterministic pseudo-random (stable look across runs).
    uint32_t s = 0x9E3779B9u;
    auto rnd = [&]() -> float {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return (float)(s % 10000) / 10000.0f;
    };
    for (int i = 0; i < kMotes; ++i) {
        data.push_back((rnd() - 0.5f) * 12.0f);  // x
        data.push_back(0.2f + rnd() * 2.8f);     // y
        data.push_back((rnd() - 0.5f) * 12.0f);  // z
        data.push_back(rnd());                    // seed
    }
    moteCount_ = kMotes;
    glGenVertexArrays(1, &moteVao_);
    glGenBuffers(1, &moteVbo_);
    glBindVertexArray(moteVao_);
    glBindBuffer(GL_ARRAY_BUFFER, moteVbo_);
    glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), data.data(),
                 GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          (void*)(3 * sizeof(float)));
    glBindVertexArray(0);
    return true;
}

void EnvironmentRenderer::Draw(const Mat4& viewProj, const Vec3& headPos,
                               float timeSec) {
    if (!ok_) return;

    // Sky: no depth test/write, drawn first, follows head translation.
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    {
        Mat4 skyModel = Mat4::Identity();
        skyModel.m[12] = headPos.x;
        skyModel.m[13] = headPos.y;
        skyModel.m[14] = headPos.z;
        Mat4 mvp = viewProj * skyModel;
        glUseProgram(skyProg_);
        glUniformMatrix4fv(skyUMvp_, 1, GL_FALSE, mvp.m);
        glBindVertexArray(skyVao_);
        glDrawElements(GL_TRIANGLES, skyIndexCount_, GL_UNSIGNED_SHORT,
                       (void*)0);
        glBindVertexArray(0);
    }

    // Ground + motes: depth-tested against each other.
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    {
        Mat4 mvp = viewProj;  // ground is already in world space
        glUseProgram(groundProg_);
        glUniformMatrix4fv(groundUMvp_, 1, GL_FALSE, mvp.m);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glBindVertexArray(groundVao_);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glBindVertexArray(0);
        glDisable(GL_BLEND);
    }
    {
        Mat4 mvp = viewProj;
        glUseProgram(moteProg_);
        glUniformMatrix4fv(moteUMvp_, 1, GL_FALSE, mvp.m);
        glUniform1f(moteUTime_, timeSec);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glBindVertexArray(moteVao_);
        glDrawArrays(GL_POINTS, 0, moteCount_);
        glBindVertexArray(0);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }
    glDisable(GL_DEPTH_TEST);
}

}  // namespace xrwrist
