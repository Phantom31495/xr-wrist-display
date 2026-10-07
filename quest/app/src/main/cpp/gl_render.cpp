#include "gl_render.h"
#include "log.h"

#include <cmath>
#include <cstring>

namespace xrwrist {

// ---------------- Vec3 ----------------
float Vec3::length() const { return sqrtf(x * x + y * y + z * z); }
Vec3 Vec3::normalized() const {
    float l = length();
    return l > 1e-6f ? (*this) * (1.0f / l) : Vec3(0, 0, 0);
}
Vec3 Vec3::cross(const Vec3& o) const {
    return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};
}

// ---------------- Quat ----------------
Vec3 Quat::rotate(const Vec3& v) const {
    // q * v * q^-1 (assumes normalized q)
    Vec3 qv{x, y, z};
    Vec3 t = qv.cross(v) * 2.0f;
    return v + t * w + qv.cross(t);
}

// ---------------- Mat4 ----------------
Mat4 Mat4::Identity() {
    Mat4 r{};
    memset(r.m, 0, sizeof(r.m));
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}

Mat4 Mat4::Perspective(float fovYRad, float aspect, float zNear, float zFar) {
    Mat4 r{};
    memset(r.m, 0, sizeof(r.m));
    float f = 1.0f / tanf(fovYRad * 0.5f);
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (zFar + zNear) / (zNear - zFar);
    r.m[11] = -1.0f;
    r.m[14] = (2.0f * zFar * zNear) / (zNear - zFar);
    return r;
}

Mat4 Mat4::FromXrFov(float angleLeft, float angleRight, float angleUp,
                     float angleDown, float zNear, float zFar) {
    // OpenXR symmetric-ish projection from individual FOV angles.
    float l = tanf(angleLeft);
    float r = tanf(angleRight);
    float u = tanf(angleUp);
    float d = tanf(angleDown);
    float w = r - l;
    float h = u - d;
    Mat4 m{};
    memset(m.m, 0, sizeof(m.m));
    m.m[0] = 2.0f / w;
    m.m[5] = 2.0f / h;
    m.m[8] = (r + l) / w;
    m.m[9] = (u + d) / h;
    m.m[10] = -(zFar + zNear) / (zFar - zNear);
    m.m[11] = -1.0f;
    m.m[14] = -(2.0f * zFar * zNear) / (zFar - zNear);
    return m;
}

Mat4 Mat4::FromPose(const Vec3& pos, const Quat& q) {
    Mat4 r = Identity();
    float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    r.m[0] = 1 - 2 * (yy + zz);
    r.m[1] = 2 * (xy + wz);
    r.m[2] = 2 * (xz - wy);
    r.m[4] = 2 * (xy - wz);
    r.m[5] = 1 - 2 * (xx + zz);
    r.m[6] = 2 * (yz + wx);
    r.m[8] = 2 * (xz + wy);
    r.m[9] = 2 * (yz - wx);
    r.m[10] = 1 - 2 * (xx + yy);
    r.m[12] = pos.x;
    r.m[13] = pos.y;
    r.m[14] = pos.z;
    return r;
}

Mat4 Mat4::LookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
    Vec3 f = (center - eye).normalized();
    Vec3 s = f.cross(up).normalized();
    Vec3 u = s.cross(f);
    Mat4 r = Identity();
    r.m[0] = s.x;
    r.m[1] = u.x;
    r.m[2] = -f.x;
    r.m[4] = s.y;
    r.m[5] = u.y;
    r.m[6] = -f.y;
    r.m[8] = s.z;
    r.m[9] = u.z;
    r.m[10] = -f.z;
    r.m[12] = -s.dot(eye);
    r.m[13] = -u.dot(eye);
    r.m[14] = f.dot(eye);
    return r;
}

Mat4 Mat4::operator*(const Mat4& o) const {
    Mat4 r{};
    for (int c = 0; c < 4; ++c) {
        for (int r_ = 0; r_ < 4; ++r_) {
            float sum = 0;
            for (int k = 0; k < 4; ++k) sum += m[k * 4 + r_] * o.m[c * 4 + k];
            r.m[c * 4 + r_] = sum;
        }
    }
    return r;
}

Mat4 Mat4::invertedRigid() const {
    // Assumes upper 3x3 is a pure rotation.
    Mat4 r = Identity();
    // Transpose rotation.
    for (int c = 0; c < 3; ++c)
        for (int rr = 0; rr < 3; ++rr) r.m[c * 4 + rr] = m[rr * 4 + c];
    Vec3 t{m[12], m[13], m[14]};
    Vec3 nt{
        -(r.m[0] * t.x + r.m[4] * t.y + r.m[8] * t.z),
        -(r.m[1] * t.x + r.m[5] * t.y + r.m[9] * t.z),
        -(r.m[2] * t.x + r.m[6] * t.y + r.m[10] * t.z),
    };
    r.m[12] = nt.x;
    r.m[13] = nt.y;
    r.m[14] = nt.z;
    return r;
}

// ---------------- QuadRenderer ----------------

namespace {
const char* kVertShader = R"(
#version 300 es
layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aUv;
uniform mat4 uMvp;
out vec2 vUv;
void main() {
    vUv = aUv;
    gl_Position = uMvp * vec4(aPos, 1.0);
}
)";
const char* kFragShader = R"(
#version 300 es
#extension GL_OES_EGL_image_external_essl3 : require
precision mediump float;
in vec2 vUv;
uniform samplerExternalOES uTex;
uniform mat4 uTexMat;
uniform vec4 uTint;
out vec4 fragColor;
void main() {
    vec2 uv = (uTexMat * vec4(vUv, 0.0, 1.0)).xy;
    vec3 c = texture(uTex, uv).rgb;
    fragColor = vec4(c * uTint.rgb, uTint.a);
}
)";

GLuint CompileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        LOGE("shader compile failed: %s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}
}  // namespace

bool QuadRenderer::Init() {
    GLuint vs = CompileShader(GL_VERTEX_SHADER, kVertShader);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, kFragShader);
    if (!vs || !fs) return false;
    prog_ = glCreateProgram();
    glAttachShader(prog_, vs);
    glAttachShader(prog_, fs);
    glLinkProgram(prog_);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(prog_, GL_LINK_STATUS, &ok);
    if (!ok) {
        LOGE("program link failed");
        return false;
    }
    uMvp_ = glGetUniformLocation(prog_, "uMvp");
    uTex_ = glGetUniformLocation(prog_, "uTex");
    uTint_ = glGetUniformLocation(prog_, "uTint");
    uTexMat_ = glGetUniformLocation(prog_, "uTexMat");

    // Quad in local space: 1x1 centered at origin, facing +Z.
    // Caller scales via MVP.
    const float verts[] = {
        // x, y, z, u, v
        -0.5f, -0.5f, 0, 0, 0,
        0.5f, -0.5f, 0, 1, 0,
        0.5f, 0.5f, 0, 1, 1,
        -0.5f, -0.5f, 0, 0, 0,
        0.5f, 0.5f, 0, 1, 1,
        -0.5f, 0.5f, 0, 0, 1,
    };
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                          (void*)(3 * sizeof(float)));
    glBindVertexArray(0);
    ok_ = true;
    LOGI("QuadRenderer init ok");
    return true;
}

void QuadRenderer::Shutdown() {
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (prog_) glDeleteProgram(prog_);
    vao_ = vbo_ = prog_ = 0;
    ok_ = false;
}

void QuadRenderer::Draw(GLuint fbo, int viewportW, int viewportH,
                        const Mat4& mvp, GLuint externalTex, const float tint[4]) {
    if (!ok_) return;
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, viewportW, viewportH);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(prog_);
    glUniformMatrix4fv(uMvp_, 1, GL_FALSE, mvp.m);
    float ident[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    glUniformMatrix4fv(uTexMat_, 1, GL_FALSE, ident);
    glUniform4fv(uTint_, 1, tint);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, externalTex);
    glUniform1i(uTex_, 0);
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glDisable(GL_BLEND);
}

}  // namespace xrwrist
