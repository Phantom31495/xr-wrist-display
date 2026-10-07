#include "avatar.h"
#include "log.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace xrwrist {
namespace {

constexpr float kPi = 3.141592653589793f;

inline float Clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

// ---------------------------------------------------------------------------
// Small matrix helpers (file-local; gl_render covers the rest).
// ---------------------------------------------------------------------------
Mat4 Trans(float x, float y, float z) {
    Mat4 r = Mat4::Identity();
    r.m[12] = x;
    r.m[13] = y;
    r.m[14] = z;
    return r;
}
Mat4 RotX(float a) {
    Mat4 r = Mat4::Identity();
    float c = cosf(a), s = sinf(a);
    r.m[5] = c;
    r.m[6] = s;
    r.m[9] = -s;
    r.m[10] = c;
    return r;
}
Mat4 RotZ(float a) {
    Mat4 r = Mat4::Identity();
    float c = cosf(a), s = sinf(a);
    r.m[0] = c;
    r.m[1] = s;
    r.m[4] = -s;
    r.m[5] = c;
    return r;
}
Mat4 Scale(float x, float y, float z) {
    Mat4 r = Mat4::Identity();
    r.m[0] = x;
    r.m[5] = y;
    r.m[10] = z;
    return r;
}
// Rotation whose local +Y maps onto yDir (orthonormal basis via hint).
Mat4 BasisFromY(const Vec3& yDir, const Vec3& hint) {
    Vec3 y = yDir.normalized();
    Vec3 x = hint.cross(y);
    if (x.length() < 1e-4f) x = Vec3(1, 0, 0);
    x = x.normalized();
    Vec3 z = x.cross(y);
    Mat4 r = Mat4::Identity();
    r.m[0] = x.x; r.m[1] = x.y; r.m[2] = x.z;
    r.m[4] = y.x; r.m[5] = y.y; r.m[6] = y.z;
    r.m[8] = z.x; r.m[9] = z.y; r.m[10] = z.z;
    return r;
}
// Rotation matrix (columns x,y,z) -> quaternion. Right-handed: x cross y = z.
Quat QuatFromBasis(const Vec3& x, const Vec3& y, const Vec3& z) {
    float m00 = x.x, m01 = y.x, m02 = z.x;
    float m10 = x.y, m11 = y.y, m12 = z.y;
    float m20 = x.z, m21 = y.z, m22 = z.z;
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

// ---------------------------------------------------------------------------
// Mesh building.
// ---------------------------------------------------------------------------
using AVert = AvatarRenderer::Vert;

inline float SPow(float t, float e) {
    return (t >= 0 ? 1.f : -1.f) * powf(fabsf(t), e);
}

// Capsule along +Y, base pole at y=0, tip at y=cylLen+2*radius.
void GenCapsule(std::vector<AVert>& verts, std::vector<uint32_t>& idx,
                float radius, float cylLen, int radialSegs, int capSegs) {
    struct Ring { float y, rr, ny; };  // ny = normal's Y component (cos phi)
    std::vector<Ring> rings;
    for (int i = 0; i <= capSegs; ++i) {  // bottom hemisphere
        float phi = -kPi / 2 + (kPi / 2) * (float)i / capSegs;
        rings.push_back({-cylLen / 2 + radius * sinf(phi), radius * cosf(phi),
                         sinf(phi)});
    }
    for (int i = 1; i <= capSegs; ++i) {  // top hemisphere
        float phi = (kPi / 2) * (float)i / capSegs;
        rings.push_back({cylLen / 2 + radius * sinf(phi), radius * cosf(phi),
                         sinf(phi)});
    }
    float yOff = cylLen / 2 + radius;  // shift so the base pole sits at y=0
    for (const auto& rg : rings) {
        float cosPhi = (radius > 0) ? rg.rr / radius : 0.f;
        for (int j = 0; j <= radialSegs; ++j) {
            float th = 2 * kPi * (float)j / radialSegs;
            float cx = cosf(th), sx = sinf(th);
            AVert v{};
            v.p[0] = rg.rr * cx;
            v.p[1] = rg.y + yOff;
            v.p[2] = rg.rr * sx;
            v.n[0] = cosPhi * cx;
            v.n[1] = rg.ny;
            v.n[2] = cosPhi * sx;
            v.uv[0] = (float)j / radialSegs;
            v.uv[1] = rg.y;
            verts.push_back(v);
        }
    }
    int ringVerts = radialSegs + 1;
    for (size_t r = 0; r + 1 < rings.size(); ++r) {
        for (int j = 0; j < radialSegs; ++j) {
            uint32_t a = (uint32_t)(r * ringVerts + j);
            uint32_t b = a + 1;
            uint32_t c = a + ringVerts;
            uint32_t d = c + 1;
            idx.insert(idx.end(), {a, c, b, b, c, d});
        }
    }
}

// Tapered limb along +Y from y=0 (radius r0) to y=len (radius r1),
// with rounded caps on both ends. Smooth analytic normals.
void GenTaperedLimb(std::vector<AVert>& verts, std::vector<uint32_t>& idx,
                    float r0, float r1, float len, int radialSegs,
                    int heightSegs) {
    // Ring normal = (radial * cos(th), ny, radial * sin(th)).
    struct Ring { float y, rr, radial, ny; };
    std::vector<Ring> rings;
    // Bottom (wrist) hemisphere cap.
    for (int i = 0; i <= 3; ++i) {
        float phi = -kPi / 2 + (kPi / 2) * (float)i / 3;
        rings.push_back(
            {r0 * sinf(phi), r0 * cosf(phi), cosf(phi), sinf(phi)});
    }
    // Tapered body. Surface r(y) = r0 + (r1 - r0) * y / len; the outward
    // normal is (cos th, (r0 - r1) / len, sin th), normalized.
    float slope = (r0 - r1) / len;
    float inv = 1.0f / sqrtf(1.0f + slope * slope);
    for (int i = 1; i <= heightSegs; ++i) {
        float y = len * (float)i / heightSegs;
        float rr = r0 + (r1 - r0) * (float)i / heightSegs;
        rings.push_back({y, rr, inv, slope * inv});
    }
    // Top (elbow) hemisphere cap.
    for (int i = 1; i <= 3; ++i) {
        float phi = (kPi / 2) * (float)i / 3;
        rings.push_back(
            {len + r1 * sinf(phi), r1 * cosf(phi), cosf(phi), sinf(phi)});
    }
    int ringVerts = radialSegs + 1;
    for (const auto& rg : rings) {
        for (int j = 0; j <= radialSegs; ++j) {
            float th = 2 * kPi * (float)j / radialSegs;
            float cx = cosf(th), sx = sinf(th);
            AVert v{};
            v.p[0] = rg.rr * cx;
            v.p[1] = rg.y;
            v.p[2] = rg.rr * sx;
            v.n[0] = rg.radial * cx;
            v.n[1] = rg.ny;
            v.n[2] = rg.radial * sx;
            v.uv[0] = (float)j / radialSegs;
            v.uv[1] = rg.y / len;
            verts.push_back(v);
        }
    }
    for (size_t r = 0; r + 1 < rings.size(); ++r) {
        for (int j = 0; j < radialSegs; ++j) {
            uint32_t a = (uint32_t)(r * ringVerts + j);
            uint32_t b = a + 1;
            uint32_t c = a + ringVerts;
            uint32_t d = c + 1;
            idx.insert(idx.end(), {a, c, b, b, c, d});
        }
    }
}

// Superellipsoid centered at the origin, half-extents (a,b,c), exponent e.
// Numeric smooth normals via central differences.
void GenSuperellipsoid(std::vector<AVert>& verts, std::vector<uint32_t>& idx,
                       float a, float b, float c, float e, int uSegs,
                       int vSegs) {
    auto pos = [&](float u, float v, float out[3]) {
        float cu = cosf(u), su = sinf(u), cv = cosf(v), sv = sinf(v);
        out[0] = a * SPow(cv, e) * SPow(cu, e);
        out[1] = b * SPow(cv, e) * SPow(su, e);
        out[2] = c * SPow(sv, e);
    };
    const float h = 1e-3f;
    for (int iv = 0; iv <= vSegs; ++iv) {
        float v = -kPi / 2 + kPi * (float)iv / vSegs;
        for (int iu = 0; iu <= uSegs; ++iu) {
            float u = -kPi + 2 * kPi * (float)iu / uSegs;
            float p[3], pu1[3], pu0[3], pv1[3], pv0[3];
            pos(u, v, p);
            pos(u + h, v, pu1);
            pos(u - h, v, pu0);
            pos(u, v + h, pv1);
            pos(u, v - h, pv0);
            float du[3] = {(pu1[0] - pu0[0]) / (2 * h),
                           (pu1[1] - pu0[1]) / (2 * h),
                           (pu1[2] - pu0[2]) / (2 * h)};
            float dv[3] = {(pv1[0] - pv0[0]) / (2 * h),
                           (pv1[1] - pv0[1]) / (2 * h),
                           (pv1[2] - pv0[2]) / (2 * h)};
            // n = du x dv (outward for this parametrization)
            float n[3] = {du[1] * dv[2] - du[2] * dv[1],
                          du[2] * dv[0] - du[0] * dv[2],
                          du[0] * dv[1] - du[1] * dv[0]};
            float nl = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            if (nl < 1e-9f) {
                // Poles lie on the Z axis.
                n[0] = 0;
                n[1] = 0;
                n[2] = (v > 0 ? 1.f : -1.f);
                nl = 1;
            }
            AVert vt{};
            vt.p[0] = p[0];
            vt.p[1] = p[1];
            vt.p[2] = p[2];
            vt.n[0] = n[0] / nl;
            vt.n[1] = n[1] / nl;
            vt.n[2] = n[2] / nl;
            vt.uv[0] = (float)iu / uSegs;
            vt.uv[1] = (float)iv / vSegs;
            verts.push_back(vt);
        }
    }
    int row = uSegs + 1;
    for (int iv = 0; iv < vSegs; ++iv) {
        for (int iu = 0; iu < uSegs; ++iu) {
            uint32_t a0 = iv * row + iu;
            uint32_t b0 = a0 + 1;
            uint32_t c0 = a0 + row;
            uint32_t d0 = c0 + 1;
            idx.insert(idx.end(), {a0, b0, c0, b0, d0, c0});
        }
    }
}

// Flat rounded-rectangle plate in the XY plane, normal +Z, uv in [0,1].
void GenRoundedRect(std::vector<AVert>& verts, std::vector<uint32_t>& idx,
                    float w, float h, float r, int cornerSegs) {
    float hw = w / 2, hh = h / 2;
    AVert center{};
    center.p[0] = center.p[1] = center.p[2] = 0;
    center.n[0] = center.n[1] = 0;
    center.n[2] = 1;
    center.uv[0] = center.uv[1] = 0.5f;
    verts.push_back(center);
    // CCW boundary starting at the right edge.
    const float cx[4] = {hw - r, -(hw - r), -(hw - r), hw - r};
    const float cy[4] = {hh - r, hh - r, -(hh - r), -(hh - r)};
    std::vector<uint32_t> ring;
    for (int c = 0; c < 4; ++c) {
        for (int i = 0; i <= cornerSegs; ++i) {
            float a = (kPi / 2) * ((float)i / cornerSegs + c);
            float x = cx[c] + r * cosf(a);
            float y = cy[c] + r * sinf(a);
            // Skip the duplicate seam point between corners.
            if (c > 0 && i == 0) continue;
            AVert v{};
            v.p[0] = x;
            v.p[1] = y;
            v.p[2] = 0;
            v.n[0] = v.n[1] = 0;
            v.n[2] = 1;
            v.uv[0] = x / w + 0.5f;
            v.uv[1] = y / h + 0.5f;
            ring.push_back((uint32_t)verts.size());
            verts.push_back(v);
        }
    }
    for (size_t i = 0; i < ring.size(); ++i) {
        idx.insert(idx.end(),
                   {0, ring[i], ring[(i + 1) % ring.size()]});
    }
}

void UploadMesh(AvatarRenderer::Mesh& mesh,
                const std::vector<AVert>& verts,
                const std::vector<uint32_t>& idx) {
    glGenVertexArrays(1, &mesh.vao);
    glBindVertexArray(mesh.vao);
    glGenBuffers(1, &mesh.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(AVert), verts.data(),
                 GL_STATIC_DRAW);
    glGenBuffers(1, &mesh.ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(uint32_t),
                 idx.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(AVert), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(AVert),
                          (void*)offsetof(AVert, n));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(AVert),
                          (void*)offsetof(AVert, uv));
    glBindVertexArray(0);
    mesh.indexCount = (GLsizei)idx.size();

    // GODMODE: unique-edge wireframe index buffer (bound explicitly at draw
    // time, not part of the VAO's element-buffer state).
    std::vector<uint64_t> edges;
    edges.reserve(idx.size());
    for (size_t i = 0; i + 2 < idx.size(); i += 3) {
        uint32_t t[3] = {idx[i], idx[i + 1], idx[i + 2]};
        for (int e = 0; e < 3; ++e) {
            uint32_t a = t[e], b = t[(e + 1) % 3];
            if (a > b) std::swap(a, b);
            edges.push_back((static_cast<uint64_t>(a) << 32) | b);
        }
    }
    std::sort(edges.begin(), edges.end());
    edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
    std::vector<uint32_t> wire;
    wire.reserve(edges.size() * 2);
    for (uint64_t e : edges) {
        wire.push_back(static_cast<uint32_t>(e >> 32));
        wire.push_back(static_cast<uint32_t>(e & 0xffffffffu));
    }
    glGenBuffers(1, &mesh.wireIbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.wireIbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, wire.size() * sizeof(uint32_t),
                 wire.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    mesh.wireIndexCount = (GLsizei)wire.size();
}

// ---------------------------------------------------------------------------
// Shaders.
// ---------------------------------------------------------------------------
const char* kVertShader = R"(
#version 300 es
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUv;
uniform mat4 uMvp;
uniform mat4 uModel;
out vec3 vNormalW;
out vec3 vWorldPos;
out vec2 vUv;
void main() {
    vNormalW = mat3(uModel) * aNormal;
    vec4 wp = uModel * vec4(aPos, 1.0);
    vWorldPos = wp.xyz;
    vUv = aUv;
    gl_Position = uMvp * vec4(aPos, 1.0);
}
)";

// Skin: wrap diffuse (cheap subsurface scattering) + back-scatter term +
// broad soft specular.
const char* kSkinFragShader = R"(
#version 300 es
precision mediump float;
in vec3 vNormalW;
in vec3 vWorldPos;
in vec2 vUv;
uniform vec3 uCamPos;
uniform vec3 uLightDir;
uniform vec3 uSkinColor;
uniform float uWrap;
out vec4 fragColor;
void main() {
    vec3 N = normalize(vNormalW);
    vec3 V = normalize(uCamPos - vWorldPos);
    vec3 L = normalize(uLightDir);
    // Wrap lighting: light bleeds around the terminator like skin.
    float ndl = clamp((dot(N, L) + uWrap) / (1.0 + uWrap), 0.0, 1.0);
    ndl = ndl * ndl * (3.0 - 2.0 * ndl);
    // Translucency: light scattering through thin tissue toward the viewer.
    float back = pow(clamp(dot(V, -L), 0.0, 1.0), 2.5);
    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), 24.0) * 0.16;
    vec3 col = uSkinColor * (0.30 + 0.90 * ndl);
    col += uSkinColor * vec3(1.0, 0.42, 0.28) * back * 0.35;
    col += vec3(1.0, 0.96, 0.90) * spec;
    fragColor = vec4(col, 1.0);
}
)";

// Dark metal: fake environment gradient + fresnel + tight specular.
const char* kMetalFragShader = R"(
#version 300 es
precision mediump float;
in vec3 vNormalW;
in vec3 vWorldPos;
in vec2 vUv;
uniform vec3 uCamPos;
uniform vec3 uLightDir;
uniform vec3 uGlowColor;  // v0.4.0: pre-detach warning tint
uniform float uGlow;      // v0.4.0: 0 = off, 1 = full glow
out vec4 fragColor;
void main() {
    vec3 N = normalize(vNormalW);
    vec3 V = normalize(uCamPos - vWorldPos);
    vec3 L = normalize(uLightDir);
    vec3 R = reflect(-V, N);
    float up = clamp(R.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 env = mix(vec3(0.012, 0.013, 0.018), vec3(0.30, 0.33, 0.38), up);
    env += vec3(0.22, 0.24, 0.27) * pow(clamp(1.0 - abs(R.y), 0.0, 1.0), 6.0);
    float fres = pow(1.0 - clamp(dot(N, V), 0.0, 1.0), 5.0);
    float spec = pow(clamp(dot(R, L), 0.0, 1.0), 90.0);
    vec3 col = vec3(0.030, 0.032, 0.038) * 0.5
             + env * (0.22 + 0.78 * fres)
             + vec3(1.0, 0.98, 0.95) * spec * 0.9;
    col += uGlowColor * uGlow;  // v0.4.0: warning glow
    fragColor = vec4(col, 1.0);
}
)";

// Watch glass: video texture with rounded-corner SDF, glass highlight band,
// and subtle edge darkening for a curved-glass feel.
const char* kGlassFragShader = R"(
#version 300 es
#extension GL_OES_EGL_image_external_essl3 : require
precision mediump float;
in vec3 vNormalW;
in vec3 vWorldPos;
in vec2 vUv;
uniform samplerExternalOES uTex;
uniform mat4 uTexMat;
uniform float uCornerR;  // corner radius in height-normalized UV units
uniform float uAspect;   // face width / face height
uniform vec2 uPulse;     // v0.4.0: touch ripple center in UV
uniform float uPulseI;   // v0.4.0: touch ripple intensity 0..1
uniform float uBrightness;  // v0.6.2: display brightness 0.3..1.5
out vec4 fragColor;
float sdRoundBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}
void main() {
    vec2 p = (vUv - 0.5) * vec2(uAspect, 1.0);
    float d = sdRoundBox(p, vec2(0.5 * uAspect, 0.5) - uCornerR, uCornerR);
    if (d > 0.0) discard;
    vec2 uv = (uTexMat * vec4(vUv, 0.0, 1.0)).xy;
    vec3 c = texture(uTex, uv).rgb;
    // v0.6.2: user-adjustable brightness (control bar).
    c *= uBrightness;
    float band = smoothstep(0.16, 0.0, abs(vUv.x * 0.65 + vUv.y - 0.72));
    c += vec3(1.0, 0.98, 0.94) * band * 0.06;
    float edge = smoothstep(0.0, 0.035, -d);
    c *= mix(0.70, 1.0, edge);
    // v0.4.0: touch ripple — soft expanding ring at the touch point.
    // v0.6.2: warm-amber tint (Soft-Tech) instead of white.
    float pd = length((vUv - uPulse) * vec2(uAspect, 1.0));
    float ring = smoothstep(0.09, 0.02, pd) * uPulseI;
    c += vec3(1.0, 0.62, 0.15) * ring * 0.65;
    fragColor = vec4(c, 1.0);
}
)";

// GODMODE: debug visualization — flat wireframe color or normals-as-RGB.
// Shares the standard vertex shader (world pos + normal varyings).
const char* kDebugFragShader = R"(
#version 300 es
precision mediump float;
in vec3 vNormalW;
in vec3 vWorldPos;
in vec2 vUv;
uniform vec3 uDebugColor;
uniform int uDebugKind;  // 0 = flat wireframe, 1 = normals
out vec4 fragColor;
void main() {
    if (uDebugKind == 1) {
        vec3 n = normalize(vNormalW) * 0.5 + 0.5;
        fragColor = vec4(n, 1.0);
    } else {
        fragColor = vec4(uDebugColor, 1.0);
    }
}
)";

GLuint CompileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        LOGE("avatar: shader compile failed: %s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

}  // namespace

// ---------------------------------------------------------------------------
// AvatarRenderer
// ---------------------------------------------------------------------------
bool AvatarRenderer::Init() {
    if (!BuildPrograms()) return false;
    if (!BuildMeshes()) return false;
    ok_ = true;
    LOGI("avatar: init ok");
    return true;
}

void AvatarRenderer::Shutdown() {
    auto delMesh = [](Mesh& m) {
        if (m.wireIbo) glDeleteBuffers(1, &m.wireIbo);
        if (m.ibo) glDeleteBuffers(1, &m.ibo);
        if (m.vbo) glDeleteBuffers(1, &m.vbo);
        if (m.vao) glDeleteVertexArrays(1, &m.vao);
        m = Mesh{};
    };
    delMesh(forearm_);
    delMesh(palm_);
    for (auto& f : fingerSeg_)
        for (auto& s : f) delMesh(s);
    for (auto& s : thumbSeg_) delMesh(s);
    delMesh(bezel_);
    delMesh(glass_);
    delMesh(crown_);
    for (Program* p : {&skinProg_, &metalProg_, &glassProg_, &debugProg_}) {
        if (p->prog) glDeleteProgram(p->prog);
        *p = Program{};
    }
    uDebugKind_ = uDebugColor_ = -1;
    ok_ = false;
}

GLuint AvatarRenderer::CompileProgram(const char* vsSrc, const char* fsSrc) {
    GLuint vs = CompileShader(GL_VERTEX_SHADER, vsSrc);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fsSrc);
    if (!vs || !fs) return 0;
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        LOGE("avatar: program link failed");
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

bool AvatarRenderer::BuildPrograms() {
    skinProg_.prog = CompileProgram(kVertShader, kSkinFragShader);
    metalProg_.prog = CompileProgram(kVertShader, kMetalFragShader);
    glassProg_.prog = CompileProgram(kVertShader, kGlassFragShader);
    debugProg_.prog = CompileProgram(kVertShader, kDebugFragShader);
    if (!skinProg_.prog || !metalProg_.prog || !glassProg_.prog ||
        !debugProg_.prog)
        return false;
    for (Program* p : {&skinProg_, &metalProg_, &glassProg_, &debugProg_}) {
        p->uMvp = glGetUniformLocation(p->prog, "uMvp");
        p->uModel = glGetUniformLocation(p->prog, "uModel");
        p->uCamPos = glGetUniformLocation(p->prog, "uCamPos");
    }
    skinProg_.uSkinColor = glGetUniformLocation(skinProg_.prog, "uSkinColor");
    skinProg_.uLightDir = glGetUniformLocation(skinProg_.prog, "uLightDir");
    skinProg_.uWrap = glGetUniformLocation(skinProg_.prog, "uWrap");
    metalProg_.uLightDir = glGetUniformLocation(metalProg_.prog, "uLightDir");
    glassProg_.uTex = glGetUniformLocation(glassProg_.prog, "uTex");
    glassProg_.uTexMat = glGetUniformLocation(glassProg_.prog, "uTexMat");
    glassProg_.uCornerR = glGetUniformLocation(glassProg_.prog, "uCornerR");
    glassProg_.uAspect = glGetUniformLocation(glassProg_.prog, "uAspect");
    glassProg_.uPulse = glGetUniformLocation(glassProg_.prog, "uPulse");
    glassProg_.uPulseI = glGetUniformLocation(glassProg_.prog, "uPulseI");
    glassProg_.uBrightness =
        glGetUniformLocation(glassProg_.prog, "uBrightness");
    metalProg_.uGlow = glGetUniformLocation(metalProg_.prog, "uGlow");
    metalProg_.uGlowColor = glGetUniformLocation(metalProg_.prog, "uGlowColor");
    uDebugKind_ = glGetUniformLocation(debugProg_.prog, "uDebugKind");
    uDebugColor_ = glGetUniformLocation(debugProg_.prog, "uDebugColor");
    return true;
}

bool AvatarRenderer::BuildMeshes() {
    using namespace avatar_dims;
    std::vector<AVert> verts;
    std::vector<uint32_t> idx;

    // Forearm: tapered limb, wrist radius -> elbow radius.
    verts.clear();
    idx.clear();
    GenTaperedLimb(verts, idx, kWristRadius, kElbowRadius, kForearmLength,
                   14, 6);
    UploadMesh(forearm_, verts, idx);

    // Palm: rounded superellipsoid.
    verts.clear();
    idx.clear();
    GenSuperellipsoid(verts, idx, kPalmHalfX, kPalmHalfY, kPalmHalfZ,
                      kPalmRound, 20, 14);
    UploadMesh(palm_, verts, idx);

    // Fingers: capsule per phalanx, base at the joint.
    for (int f = 0; f < 4; ++f) {
        for (int s = 0; s < 3; ++s) {
            verts.clear();
            idx.clear();
            GenCapsule(verts, idx, kFingers[f].radius, kFingers[f].segLen[s],
                       10, 4);
            UploadMesh(fingerSeg_[f][s], verts, idx);
        }
    }

    // Thumb: two segments.
    for (int s = 0; s < 2; ++s) {
        verts.clear();
        idx.clear();
        GenCapsule(verts, idx, kThumbRadius, kThumbSegLen[s], 10, 4);
        UploadMesh(thumbSeg_[s], verts, idx);
    }

    // Watch bezel: rounded-rect plate, dark metal.
    verts.clear();
    idx.clear();
    GenRoundedRect(verts, idx, kWatchFaceW + 2 * kBezelWidth,
                   kWatchFaceH + 2 * kBezelWidth, kBezelCornerR, 6);
    UploadMesh(bezel_, verts, idx);

    // Watch glass: rounded-rect video face.
    verts.clear();
    idx.clear();
    GenRoundedRect(verts, idx, kWatchFaceW, kWatchFaceH, kGlassCornerR, 8);
    UploadMesh(glass_, verts, idx);

    // Crown: small capsule on the watch's +X edge.
    verts.clear();
    idx.clear();
    GenCapsule(verts, idx, 0.004f, 0.006f, 10, 3);
    UploadMesh(crown_, verts, idx);

    return true;
}

void AvatarRenderer::DrawMesh(const Mesh& mesh, Program& p, const Mat4& mvp,
                              const Mat4& model) {
    // Caller must have bound p.prog and set shared uniforms (solid path).
    if (debugMode_ == DebugMode::Off) {
        glUniformMatrix4fv(p.uMvp, 1, GL_FALSE, mvp.m);
        glUniformMatrix4fv(p.uModel, 1, GL_FALSE, model.m);
        glBindVertexArray(mesh.vao);
        // Rebind explicitly: wireframe mode swaps the VAO's element buffer.
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ibo);
        glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);
    } else {
        // GODMODE: debug visualization through the shared debug program.
        glUseProgram(debugProg_.prog);
        glUniformMatrix4fv(debugProg_.uMvp, 1, GL_FALSE, mvp.m);
        glUniformMatrix4fv(debugProg_.uModel, 1, GL_FALSE, model.m);
        glBindVertexArray(mesh.vao);
        if (debugMode_ == DebugMode::Wireframe) {
            glUniform1i(uDebugKind_, 0);
            glUniform3f(uDebugColor_, 0.1f, 0.9f, 1.0f);  // cyan edges
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.wireIbo);
            glDrawElements(GL_LINES, mesh.wireIndexCount, GL_UNSIGNED_INT,
                           nullptr);
        } else {  // Normals
            glUniform1i(uDebugKind_, 1);
            glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT,
                           nullptr);
        }
        glBindVertexArray(0);
    }
    stats_.drawCalls++;
    stats_.triangles += mesh.indexCount / 3;
}

void AvatarRenderer::ComputeWatchPose(const XrInput::Pose& grip,
                                     const Vec3& headPos, Vec3& outPos,
                                     Quat& outQuat) {
    using namespace avatar_dims;
    // Face center floats above the wrist, slightly toward the fingers.
    outPos = grip.pos +
             grip.quat.rotate(Vec3(0.0f, kWatchLiftY, kWatchLiftZ));
    Vec3 wristUp = grip.quat.rotate(Vec3(0, 1, 0));  // back of the hand
    Vec3 n;
    if (headPos.length() > 1e-4f) {
        Vec3 toHead = (headPos - outPos).normalized();
        n = (toHead * kWatchHeadBias + wristUp * (1.0f - kWatchHeadBias))
                .normalized();
    } else {
        n = wristUp;
    }
    // Face "up" follows the fingers (like a real watch); projected onto the
    // face plane so the screen stays readable as the wrist turns.
    Vec3 fingerDir = grip.quat.rotate(Vec3(0, 0, -1));
    Vec3 up = fingerDir - n * fingerDir.dot(n);
    if (up.length() < 1e-4f) {
        up = wristUp - n * wristUp.dot(n);
    }
    up = up.normalized();
    Vec3 x = up.cross(n);  // right-handed: x = y cross z
    outQuat = QuatFromBasis(x, up, n);
}

void AvatarRenderer::DrawHand(const Mat4& viewProj, const Vec3& camPos,
                              const XrInput::Pose& grip,
                              const HandPose& hand, bool mirror) {
    if (!ok_ || !grip.valid) return;
    using namespace avatar_dims;

    glUseProgram(skinProg_.prog);
    glUniform3f(skinProg_.uCamPos, camPos.x, camPos.y, camPos.z);
    glUniform3f(skinProg_.uLightDir, 0.35f, 0.85f, 0.40f);
    glUniform3f(skinProg_.uSkinColor, kSkinR, kSkinG, kSkinB);
    glUniform1f(skinProg_.uWrap, kSkinWrap);

    // Hand root: grip pose, mirrored across local X for the right hand.
    // (Culling is disabled in this pass, so the flipped winding is harmless;
    // mirrored normals stay correct under the orthogonal mirror.)
    Mat4 gripW = Mat4::FromPose(grip.pos, grip.quat);
    if (mirror) gripW = gripW * Scale(-1.0f, 1.0f, 1.0f);

    // Forearm: wrist joint -> elbow direction (back and down in grip space).
    Mat4 foreNode = gripW *
                    Trans(kWristJointX, kWristJointY, kWristJointZ) *
                    RotX(kForearmPitchDeg * kPi / 180.0f);
    DrawMesh(forearm_, skinProg_, viewProj * foreNode, foreNode);

    // Palm.
    Mat4 palmNode =
        gripW * Trans(kPalmCenterX, kPalmCenterY, kPalmCenterZ);
    DrawMesh(palm_, skinProg_, viewProj * palmNode, palmNode);

    // Fingers: knuckle -> proximal -> middle -> distal, curling toward palm.
    for (int f = 0; f < 4; ++f) {
        const auto& spec = kFingers[f];
        float curl = (f == 0) ? fmaxf(hand.indexCurl, hand.gripCurl)
                              : hand.gripCurl;
        curl = Clamp01(curl);
        Mat4 node = palmNode * Trans(spec.knuckleX, kKnuckleY, kKnuckleZ) *
                    RotX(-kPi / 2 - curl * kCurlProximal);
        DrawMesh(fingerSeg_[f][0], skinProg_, viewProj * node, node);
        node = node * Trans(0.0f, spec.segLen[0], 0.0f) *
               RotX(-curl * kCurlMiddle);
        DrawMesh(fingerSeg_[f][1], skinProg_, viewProj * node, node);
        node = node * Trans(0.0f, spec.segLen[1], 0.0f) *
               RotX(-curl * kCurlDistal);
        DrawMesh(fingerSeg_[f][2], skinProg_, viewProj * node, node);
    }

    // Thumb: base orientation from its anatomical direction; flexion curls
    // toward the palm (local +Z rotation), stick-X abducts sideways.
    Vec3 thumbDir(kThumbDirX, kThumbDirY, kThumbDirZ);
    // thumbFlex: +1 (stick up) = extended, -1 (stick down) = flexed.
    float flexExtend = Clamp01((hand.thumbFlex + 1.0f) * 0.5f);
    float flex = 0.15f + (1.0f - flexExtend) * kThumbCurl;
    Mat4 thumbBase = palmNode *
                     Trans(kThumbBaseX, kThumbBaseY, kThumbBaseZ) *
                     BasisFromY(thumbDir, Vec3(0, 0, -1)) *
                     RotX(hand.thumbAbduct * kThumbAbductMax);
    Mat4 tnode = thumbBase * RotZ(flex * 0.6f);
    DrawMesh(thumbSeg_[0], skinProg_, viewProj * tnode, tnode);
    tnode = tnode * Trans(0.0f, kThumbSegLen[0], 0.0f) * RotZ(flex);
    DrawMesh(thumbSeg_[1], skinProg_, viewProj * tnode, tnode);
}

void AvatarRenderer::DrawWatch(const Mat4& viewProj, const Vec3& camPos,
                               const Vec3& watchPos, const Quat& watchQuat,
                               GLuint videoTex) {
    if (!ok_) return;
    using namespace avatar_dims;

    // v0.4.0: dynamic sizing scales the whole watch around the face center.
    Mat4 watchW = Mat4::FromPose(watchPos, watchQuat) *
                  Scale(watchScale_, watchScale_, watchScale_);

    // Bezel + crown: dark metal (v0.4.0: warning glow on the bezel).
    glUseProgram(metalProg_.prog);
    glUniform3f(metalProg_.uCamPos, camPos.x, camPos.y, camPos.z);
    glUniform3f(metalProg_.uLightDir, 0.35f, 0.85f, 0.40f);
    glUniform1f(metalProg_.uGlow, watchGlow_);
    glUniform3f(metalProg_.uGlowColor, 1.0f, 0.62f, 0.15f);  // warm amber
    DrawMesh(bezel_, metalProg_, viewProj * watchW, watchW);
    Mat4 crownM = watchW *
                  Trans(kWatchFaceW / 2 + kBezelWidth + 0.002f, 0.012f, 0.0f) *
                  RotZ(kPi / 2);
    DrawMesh(crown_, metalProg_, viewProj * crownM, crownM);

    // Glass: video face with rounded corners, floating just above the bezel.
    glUseProgram(glassProg_.prog);
    glUniform3f(glassProg_.uCamPos, camPos.x, camPos.y, camPos.z);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, videoTex);
    glUniform1i(glassProg_.uTex, 0);
    float ident[16] = {1, 0, 0, 0, 0, 1, 0, 0,
                       0, 0, 1, 0, 0, 0, 0, 1};
    glUniformMatrix4fv(glassProg_.uTexMat, 1, GL_FALSE, ident);
    glUniform1f(glassProg_.uCornerR, kGlassCornerR / kWatchFaceH);
    glUniform1f(glassProg_.uAspect, kWatchFaceW / kWatchFaceH);
    glUniform2f(glassProg_.uPulse, pulseU_, pulseV_);
    glUniform1f(glassProg_.uPulseI, pulseI_);
    glUniform1f(glassProg_.uBrightness, brightness_);
    Mat4 glassM = watchW * Trans(0.0f, 0.0f, 0.0012f);
    DrawMesh(glass_, glassProg_, viewProj * glassM, glassM);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);
}

// v0.4.0: Meta-fluent floating panel for fixed mode. Dark metal frame +
// rounded video glass, sized w x h. Reuses the watch meshes scaled to fit.
void AvatarRenderer::DrawPanel(const Mat4& viewProj, const Vec3& camPos,
                               const Vec3& pos, const Quat& quat,
                               float w, float h, GLuint videoTex) {
    if (!ok_) return;
    using namespace avatar_dims;

    Mat4 panelW = Mat4::FromPose(pos, quat);

    // Frame: bezel mesh scaled to panel size (dark metal chrome).
    float bsx = w / (kWatchFaceW + 2 * kBezelWidth);
    float bsy = h / (kWatchFaceH + 2 * kBezelWidth);
    glUseProgram(metalProg_.prog);
    glUniform3f(metalProg_.uCamPos, camPos.x, camPos.y, camPos.z);
    glUniform3f(metalProg_.uLightDir, 0.35f, 0.85f, 0.40f);
    glUniform1f(metalProg_.uGlow, 0.0f);
    glUniform3f(metalProg_.uGlowColor, 0.0f, 0.0f, 0.0f);
    Mat4 frameM = panelW * Scale(bsx, bsy, 1.0f);
    DrawMesh(bezel_, metalProg_, viewProj * frameM, frameM);

    // Video glass inset within the frame.
    glUseProgram(glassProg_.prog);
    glUniform3f(glassProg_.uCamPos, camPos.x, camPos.y, camPos.z);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, videoTex);
    glUniform1i(glassProg_.uTex, 0);
    float ident[16] = {1, 0, 0, 0, 0, 1, 0, 0,
                       0, 0, 1, 0, 0, 0, 0, 1};
    glUniformMatrix4fv(glassProg_.uTexMat, 1, GL_FALSE, ident);
    glUniform1f(glassProg_.uCornerR, kGlassCornerR / kWatchFaceH);
    glUniform1f(glassProg_.uAspect, w / h);
    glUniform2f(glassProg_.uPulse, pulseU_, pulseV_);
    glUniform1f(glassProg_.uPulseI, pulseI_);
    glUniform1f(glassProg_.uBrightness, brightness_);
    float gsx = w / kWatchFaceW;
    float gsy = h / kWatchFaceH;
    Mat4 glassM = panelW * Trans(0.0f, 0.0f, 0.0012f) * Scale(gsx, gsy, 1.0f);
    DrawMesh(glass_, glassProg_, viewProj * glassM, glassM);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);
}

}  // namespace xrwrist
