#pragma once
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <cstdint>

namespace xrwrist {

struct Vec3 {
    float x, y, z;
    Vec3() : x(0), y(0), z(0) {}
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    float dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    float length() const;
    Vec3 normalized() const;
    Vec3 cross(const Vec3& o) const;
};

struct Quat {
    float x, y, z, w;
    Quat() : x(0), y(0), z(0), w(1) {}
    Quat(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
    // Rotate vector by this quaternion.
    Vec3 rotate(const Vec3& v) const;
};

// Column-major 4x4, compatible with glUniformMatrix4fv.
struct Mat4 {
    float m[16];
    static Mat4 Identity();
    static Mat4 Perspective(float fovYRad, float aspect, float zNear, float zFar);
    // Symmetric perspective from OpenXR FOV (angles in radians).
    static Mat4 FromXrFov(float angleLeft, float angleRight, float angleUp,
                          float angleDown, float zNear, float zFar);
    static Mat4 FromPose(const Vec3& pos, const Quat& q);
    static Mat4 LookAt(const Vec3& eye, const Vec3& center, const Vec3& up);
    Mat4 operator*(const Mat4& o) const;
    Mat4 invertedRigid() const;  // inverse for rotation+translation only
};

// Renders a textured quad (external OES texture) into a bound framebuffer.
class QuadRenderer {
public:
    QuadRenderer() = default;
    ~QuadRenderer() { Shutdown(); }

    bool Init();
    void Shutdown();
    // fbo must be bound by caller; viewportW/H set the glViewport.
    // tint multiplies the sampled color (use for mode indicator).
    void Draw(GLuint fbo, int viewportW, int viewportH, const Mat4& mvp,
              GLuint externalTex, const float tint[4]);

private:
    GLuint prog_ = 0;
    GLuint vbo_ = 0;
    GLuint vao_ = 0;
    GLint uMvp_ = -1;
    GLint uTex_ = -1;
    GLint uTint_ = -1;
    GLint uTexMat_ = -1;
    bool ok_ = false;
};

}  // namespace xrwrist
