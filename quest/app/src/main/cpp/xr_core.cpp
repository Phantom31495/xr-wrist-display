#include "xr_core.h"
#include "log.h"
#include "voice.h"
#include "config.h"
#include "notify.h"

#include <cstring>
#include <cmath>
#include <vector>
#include <cctype>

namespace xrwrist {
namespace {

// v0.4.0 helpers ------------------------------------------------------------
inline float Clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

// Ease-out-back: overshoots slightly, reads as a physical "opening".
inline float EaseOutBack(float t) {
    const float c1 = 1.70158f;
    const float c3 = c1 + 1.0f;
    t = Clamp01(t);
    return 1.0f + c3 * (t - 1.0f) * (t - 1.0f) * (t - 1.0f) +
           c1 * (t - 1.0f) * (t - 1.0f);
}

// Normalized lerp for quaternions (fine for the small angles in transitions).
inline Quat Nlerp(const Quat& a, const Quat& b, float t) {
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

// Fingertip vs quad plane: signed distance + UV (with small margin so
// near-edge touches register). Returns false when far outside the quad.
inline bool FingerQuadIntersect(const Vec3& p, const Vec3& quadCenter,
                                const Quat& quadOrient, float quadW,
                                float quadH, float& outU, float& outV,
                                float& outDist) {
    Vec3 n = quadOrient.rotate(Vec3(0, 0, 1));
    Vec3 right = quadOrient.rotate(Vec3(1, 0, 0));
    Vec3 up = quadOrient.rotate(Vec3(0, 1, 0));
    Vec3 d = p - quadCenter;
    outDist = d.dot(n);
    float u = d.dot(right) / quadW + 0.5f;
    float v = 0.5f - d.dot(up) / quadH;  // v=0 at top
    if (u < -0.05f || u > 1.05f || v < -0.05f || v > 1.05f) return false;
    outU = u < 0 ? 0 : (u > 1 ? 1 : u);
    outV = v < 0 ? 0 : (v > 1 ? 1 : v);
    return true;
}

const char* kExtGlEs = "XR_KHR_opengl_es_enable";
const char* kExtPassthrough = "XR_FB_passthrough";
const char* kExtHandTracking = "XR_EXT_hand_tracking";
const char* kExtRefreshRate = "XR_FB_display_refresh_rate";
// Probed informationally (capability reporting); not enabled here.
const char* kExtHandMesh = "XR_FB_hand_tracking_mesh";
const char* kExtHandAim = "XR_FB_hand_tracking_aim";

#define XR_CHECK(call, msg)                                                    \
    do {                                                                       \
        XrResult _r = (call);                                                  \
        if (_r != XR_SUCCESS) {                                                \
            LOGE("%s failed: %d", (msg), (int)_r);                             \
            return false;                                                      \
        }                                                                      \
    } while (0)

void* LoadLoaderFunc(XrInstance instance, const char* name) {
    PFN_xrVoidFunction fn = nullptr;
    if (xrGetInstanceProcAddr(instance, name, &fn) != XR_SUCCESS || !fn) {
        LOGW("xrGetInstanceProcAddr(%s) failed", name);
        return nullptr;
    }
    return reinterpret_cast<void*>(fn);
}

// Billboard orientation: local +Z faces the head. Shared by the video quad
// and the diagnostics panel.
Quat BillboardQuat(const Vec3& headPos, const Vec3& objPos) {
    Vec3 zAxis = (headPos - objPos).normalized();
    Vec3 xAxis = Vec3(0, 1, 0).cross(zAxis).normalized();
    if (xAxis.length() < 1e-4f) xAxis = Vec3(1, 0, 0);
    Vec3 yAxis = zAxis.cross(xAxis);
    // Rotation matrix columns -> quaternion.
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

}  // namespace

// KHR_create_context attribs (define locally in case eglext.h is old).
#ifndef EGL_CONTEXT_MAJOR_VERSION_KHR
#define EGL_CONTEXT_MAJOR_VERSION_KHR 0x3098
#endif
#ifndef EGL_CONTEXT_MINOR_VERSION_KHR
#define EGL_CONTEXT_MINOR_VERSION_KHR 0x30FB
#endif

bool XrApp::InitEgl() {
    eglDisplay_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (eglDisplay_ == EGL_NO_DISPLAY) {
        LOGE("eglGetDisplay failed");
        return false;
    }
    EGLint eglMajor = 0, eglMinor = 0;
    if (!eglInitialize(eglDisplay_, &eglMajor, &eglMinor)) {
        LOGE("eglInitialize failed");
        return false;
    }
    LOGI("EGL version: %d.%d (%s)", eglMajor, eglMinor,
         eglQueryString(eglDisplay_, EGL_VERSION));
    LOGI("EGL vendor: %s", eglQueryString(eglDisplay_, EGL_VENDOR));
    LOGI("EGL client APIs: %s", eglQueryString(eglDisplay_, EGL_CLIENT_APIS));

    // NOTE: EGL_CONFIG_CAVEAT/EGL_NONE is required — the Quest runtime
    // rejects slow configs with XR_ERROR_GRAPHICS_DEVICE_INVALID (-50).
    const EGLint attribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 16,
        EGL_CONFIG_CAVEAT, EGL_NONE,
        EGL_NONE,
    };
    EGLint numConfigs = 0;
    if (!eglChooseConfig(eglDisplay_, attribs, &eglConfig_, 1, &numConfigs) ||
        numConfigs == 0) {
        LOGE("eglChooseConfig failed");
        return false;
    }
    EGLint cfgId = 0, r = 0, g = 0, b = 0, a = 0, d = 0, caveat = 0;
    eglGetConfigAttrib(eglDisplay_, eglConfig_, EGL_CONFIG_ID, &cfgId);
    eglGetConfigAttrib(eglDisplay_, eglConfig_, EGL_RED_SIZE, &r);
    eglGetConfigAttrib(eglDisplay_, eglConfig_, EGL_GREEN_SIZE, &g);
    eglGetConfigAttrib(eglDisplay_, eglConfig_, EGL_BLUE_SIZE, &b);
    eglGetConfigAttrib(eglDisplay_, eglConfig_, EGL_ALPHA_SIZE, &a);
    eglGetConfigAttrib(eglDisplay_, eglConfig_, EGL_DEPTH_SIZE, &d);
    eglGetConfigAttrib(eglDisplay_, eglConfig_, EGL_CONFIG_CAVEAT, &caveat);
    LOGI("EGL config id=%d rgba=%d%d%d%d depth=%d caveat=%d", cfgId, r, g, b, a,
         d, caveat);

    // Request the ES version the XR runtime requires (major is always 3).
    // Our shaders are "#version 300 es" which is forward-compatible with 3.x.
    const EGLint ctxAttribs[] = {
        EGL_CONTEXT_MAJOR_VERSION_KHR, 3,
        EGL_CONTEXT_MINOR_VERSION_KHR, glEsMinorRequired_,
        EGL_NONE,
    };
    LOGI("requesting EGL context ES 3.%d (runtime minimum)", glEsMinorRequired_);
    eglContext_ = eglCreateContext(eglDisplay_, eglConfig_, EGL_NO_CONTEXT, ctxAttribs);
    if (eglContext_ == EGL_NO_CONTEXT) {
        // Fall back to plain ES 3.0 if the versioned request fails.
        LOGW("versioned eglCreateContext failed, retrying ES 3.0");
        const EGLint fallbackAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        eglContext_ =
            eglCreateContext(eglDisplay_, eglConfig_, EGL_NO_CONTEXT, fallbackAttribs);
    }
    if (eglContext_ == EGL_NO_CONTEXT) {
        LOGE("eglCreateContext failed");
        return false;
    }
    const EGLint pbAttribs[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
    eglPbuffer_ = eglCreatePbufferSurface(eglDisplay_, eglConfig_, pbAttribs);
    if (eglPbuffer_ == EGL_NO_SURFACE) {
        LOGE("eglCreatePbufferSurface failed");
        return false;
    }
    if (!eglMakeCurrent(eglDisplay_, eglPbuffer_, eglPbuffer_, eglContext_)) {
        LOGE("eglMakeCurrent failed");
        return false;
    }
    LOGI("EGL ready: %s", eglQueryString(eglDisplay_, EGL_VERSION));
    return true;
}

bool XrApp::InitXrLoader() {
    // Per Meta docs: fetch xrInitializeLoaderKHR via xrGetInstanceProcAddr
    // with a null instance, call it with XrLoaderInitInfoAndroidKHR, then
    // create the instance. The function is not exported for direct linking
    // by the Khronos Android loader, but is available via the proc addr.
    PFN_xrInitializeLoaderKHR pfnInit = nullptr;
    XrResult res = xrGetInstanceProcAddr(
        XR_NULL_HANDLE, "xrInitializeLoaderKHR",
        reinterpret_cast<PFN_xrVoidFunction*>(&pfnInit));
    if (res != XR_SUCCESS || !pfnInit) {
        LOGE("xrGetInstanceProcAddr(xrInitializeLoaderKHR) failed: %d", (int)res);
        return false;
    }

    XrLoaderInitInfoAndroidKHR initInfo{XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
    initInfo.applicationVM = app_->activity->vm;
    initInfo.applicationContext = app_->activity->clazz;
    res = pfnInit(reinterpret_cast<const XrLoaderInitInfoBaseHeaderKHR*>(&initInfo));
    if (res != XR_SUCCESS) {
        LOGE("xrInitializeLoaderKHR failed: %d", (int)res);
        return false;
    }
    LOGI("OpenXR loader initialized");
    return true;
}

bool XrApp::InitXrInstance() {
    XrApplicationInfo appInfo{};
    strcpy(appInfo.applicationName, "XR Wrist Display");
    appInfo.applicationVersion = 1;
    strcpy(appInfo.engineName, "xrwrist");
    appInfo.engineVersion = 1;
    appInfo.apiVersion = XR_CURRENT_API_VERSION;

    // Enumerate supported instance extensions so optional ones (passthrough)
    // are only requested when present. xrCreateInstance fails outright if a
    // requested extension is not supported.
    uint32_t extCount = 0;
    XR_CHECK(xrEnumerateInstanceExtensionProperties(nullptr, 0, &extCount, nullptr),
             "xrEnumerateInstanceExtensionProperties(count)");
    std::vector<XrExtensionProperties> extProps(extCount, {XR_TYPE_EXTENSION_PROPERTIES});
    XR_CHECK(xrEnumerateInstanceExtensionProperties(nullptr, extCount, &extCount, extProps.data()),
             "xrEnumerateInstanceExtensionProperties(list)");
    auto hasExt = [&](const char* name) {
        for (const auto& p : extProps) {
            if (strcmp(p.extensionName, name) == 0) return true;
        }
        return false;
    };

    passthroughSupported_ = hasExt(kExtPassthrough);
    if (!passthroughSupported_) {
        LOGW("XR_FB_passthrough not supported by runtime; continuing without passthrough");
    }
    // VRC: never request an extension the runtime doesn't advertise —
    // xrCreateInstance fails outright otherwise.
    handTrackingSupported_ = hasExt(kExtHandTracking);
    if (!handTrackingSupported_) {
        LOGW("XR_EXT_hand_tracking not supported by runtime; continuing without it");
    }
    refreshRateSupported_ = hasExt(kExtRefreshRate);
    if (!refreshRateSupported_) {
        LOGW("XR_FB_display_refresh_rate not supported; using default rate");
    }
    // Informational capability probes (reported in device info, not enabled).
    handMeshSupported_ = hasExt(kExtHandMesh);
    handAimSupported_ = hasExt(kExtHandAim);
    LOGI("caps: handMesh=%d handAim=%d", handMeshSupported_ ? 1 : 0,
         handAimSupported_ ? 1 : 0);

    std::vector<const char*> extensions;
    extensions.push_back(kExtGlEs);
    if (passthroughSupported_) extensions.push_back(kExtPassthrough);
    if (handTrackingSupported_) extensions.push_back(kExtHandTracking);
    if (refreshRateSupported_) extensions.push_back(kExtRefreshRate);

    XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};
    ci.applicationInfo = appInfo;
    ci.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    ci.enabledExtensionNames = extensions.data();
    XR_CHECK(xrCreateInstance(&ci, &instance_), "xrCreateInstance");

    XrSystemGetInfo sysInfo{XR_TYPE_SYSTEM_GET_INFO};
    sysInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XR_CHECK(xrGetSystem(instance_, &sysInfo, &systemId_), "xrGetSystem");
    LOGI("OpenXR instance+system ready");

    if (!QueryGraphicsRequirements()) {
        LOGW("graphics requirements query failed; assuming ES 3.0");
        glEsMinorRequired_ = 0;
    }
    return true;
}

bool XrApp::QueryGraphicsRequirements() {
    auto pfn =
        (PFN_xrGetOpenGLESGraphicsRequirementsKHR)LoadLoaderFunc(
            instance_, "xrGetOpenGLESGraphicsRequirementsKHR");
    if (!pfn) {
        LOGE("xrGetOpenGLESGraphicsRequirementsKHR not available");
        return false;
    }
    XrGraphicsRequirementsOpenGLESKHR req{
        XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};
    XrResult r = pfn(instance_, systemId_, &req);
    if (r != XR_SUCCESS) {
        LOGE("xrGetOpenGLESGraphicsRequirementsKHR failed: %d", (int)r);
        return false;
    }
    uint32_t minMajor = XR_VERSION_MAJOR(req.minApiVersionSupported);
    glEsMinorRequired_ = (int)XR_VERSION_MINOR(req.minApiVersionSupported);
    LOGI("GL ES requirements: min %u.%d, max %u.%u", minMajor, glEsMinorRequired_,
         (unsigned)XR_VERSION_MAJOR(req.maxApiVersionSupported),
         (unsigned)XR_VERSION_MINOR(req.maxApiVersionSupported));
    if (minMajor != 3) {
        LOGW("unexpected GL ES major version %u required", minMajor);
    }
    return true;
}

bool XrApp::InitXrSession() {
    XrGraphicsBindingOpenGLESAndroidKHR gfx{XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};
    gfx.display = eglDisplay_;
    gfx.config = eglConfig_;
    gfx.context = eglContext_;
    LOGI("graphics binding: display=%p config=%p context=%p",
         (void*)gfx.display, (void*)gfx.config, (void*)gfx.context);

    XrSessionCreateInfo sci{XR_TYPE_SESSION_CREATE_INFO};
    sci.next = &gfx;
    sci.systemId = systemId_;
    XrResult sr = xrCreateSession(instance_, &sci, &session_);
    if (sr != XR_SUCCESS) {
        LOGE("xrCreateSession failed: %d", (int)sr);
        return false;
    }
    LOGI("xrCreateSession succeeded");

    // Reference spaces.
    XrReferenceSpaceCreateInfo rsci{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    rsci.poseInReferenceSpace.orientation.w = 1.0f;
    rsci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    XR_CHECK(xrCreateReferenceSpace(session_, &rsci, &localSpace_), "local space");
    rsci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    XR_CHECK(xrCreateReferenceSpace(session_, &rsci, &viewSpace_), "view space");

    // View configuration.
    uint32_t count = 0;
    XR_CHECK(xrEnumerateViewConfigurationViews(instance_, systemId_,
                                               XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                               0, &count, nullptr),
             "enum view count");
    viewCount_ = count;
    views_.resize(viewCount_, {XR_TYPE_VIEW});
    LOGI("view count: %u", viewCount_);

    InitRefreshRate();  // optional; keeps default on failure
    return true;
}

void XrApp::InitRefreshRate() {
    // VRC Performance: request the highest refresh rate the runtime offers
    // (up to 120 Hz on Quest 3/3S/2). A single quad + a small avatar are
    // trivially cheap to render, so the higher rate is free UX. Optional —
    // keeps the runtime default on failure.
    if (!refreshRateSupported_) return;
    auto pfnEnum = (PFN_xrEnumerateDisplayRefreshRatesFB)LoadLoaderFunc(
        instance_, "xrEnumerateDisplayRefreshRatesFB");
    auto pfnRequest = (PFN_xrRequestDisplayRefreshRateFB)LoadLoaderFunc(
        instance_, "xrRequestDisplayRefreshRateFB");
    if (!pfnEnum || !pfnRequest) {
        LOGI("refresh rate: XR_FB_display_refresh_rate unavailable; using default");
        return;
    }
    uint32_t rateCount = 0;
    if (pfnEnum(session_, 0, &rateCount, nullptr) != XR_SUCCESS || rateCount == 0) {
        return;
    }
    std::vector<float> rates(rateCount);
    if (pfnEnum(session_, rateCount, &rateCount, rates.data()) != XR_SUCCESS) {
        return;
    }
    float want = 0.0f;
    for (float r : rates) {
        if (r <= 120.0f && r > want) want = r;  // highest offered at or below 120
    }
    if (want <= 0.0f) want = rates[0];
    if (pfnRequest(session_, want) == XR_SUCCESS) {
        LOGI("refresh rate: requested %.0f Hz", want);
    } else {
        LOGW("refresh rate: request for %.0f Hz failed", want);
    }
}

bool XrApp::InitSwapchains() {
    // Enumerate formats, prefer SRGB8_ALPHA8.
    uint32_t fmtCount = 0;
    XR_CHECK(xrEnumerateSwapchainFormats(session_, 0, &fmtCount, nullptr),
             "enum format count");
    std::vector<int64_t> formats(fmtCount);
    XR_CHECK(xrEnumerateSwapchainFormats(session_, fmtCount, &fmtCount, formats.data()),
             "enum formats");
    int64_t chosen = 0;
    for (int64_t f : formats) {
        if (f == GL_SRGB8_ALPHA8) {
            chosen = f;
            break;
        }
    }
    if (!chosen) chosen = formats[0];
    LOGI("swapchain format: 0x%llx", (long long)chosen);

    // View dims.
    std::vector<XrViewConfigurationView> cfgViews(viewCount_,
                                                   {XR_TYPE_VIEW_CONFIGURATION_VIEW});
    uint32_t count = viewCount_;
    XR_CHECK(xrEnumerateViewConfigurationViews(instance_, systemId_,
                                               XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                               viewCount_, &count, cfgViews.data()),
             "enum view configs");

    swapchains_.resize(viewCount_);
    for (uint32_t i = 0; i < viewCount_; ++i) {
        auto& sc = swapchains_[i];
        XrSwapchainCreateInfo sci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        sci.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT |
                         XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
        sci.format = chosen;
        sci.sampleCount = 1;
        sc.width = sci.width = cfgViews[i].recommendedImageRectWidth;
        sc.height = sci.height = cfgViews[i].recommendedImageRectHeight;
        sci.faceCount = 1;
        sci.arraySize = 1;
        sci.mipCount = 1;
        XR_CHECK(xrCreateSwapchain(session_, &sci, &sc.handle), "xrCreateSwapchain");

        uint32_t imgCount = 0;
        XR_CHECK(xrEnumerateSwapchainImages(sc.handle, 0, &imgCount, nullptr),
                 "enum image count");
        sc.images.resize(imgCount, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
        XR_CHECK(xrEnumerateSwapchainImages(sc.handle, imgCount, &imgCount,
                                            (XrSwapchainImageBaseHeader*)sc.images.data()),
                 "enum images");

        sc.fbos.resize(imgCount);
        glGenFramebuffers((GLsizei)imgCount, sc.fbos.data());
        for (uint32_t j = 0; j < imgCount; ++j) {
            glBindFramebuffer(GL_FRAMEBUFFER, sc.fbos[j]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_2D, sc.images[j].image, 0);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                LOGE("FBO incomplete for swapchain %u image %u", i, j);
                return false;
            }
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        LOGI("swapchain %u: %dx%d, %u images", i, sc.width, sc.height, imgCount);

        // Shared depth buffer for the avatar/watch pass (attached per frame).
        glGenRenderbuffers(1, &sc.depthRbo);
        glBindRenderbuffer(GL_RENDERBUFFER, sc.depthRbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, sc.width,
                              sc.height);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
    }
    return true;
}

bool XrApp::InitPassthrough() {
    if (!passthroughSupported_) {
        LOGW("passthrough: extension not enabled, disabled");
        return false;
    }
    xrCreatePassthroughFB_ = (PFN_xrCreatePassthroughFB)LoadLoaderFunc(
        instance_, "xrCreatePassthroughFB");
    xrDestroyPassthroughFB_ = (PFN_xrDestroyPassthroughFB)LoadLoaderFunc(
        instance_, "xrDestroyPassthroughFB");
    xrPassthroughStartFB_ = (PFN_xrPassthroughStartFB)LoadLoaderFunc(
        instance_, "xrPassthroughStartFB");
    xrPassthroughPauseFB_ = (PFN_xrPassthroughPauseFB)LoadLoaderFunc(
        instance_, "xrPassthroughPauseFB");
    xrCreatePassthroughLayerFB_ = (PFN_xrCreatePassthroughLayerFB)LoadLoaderFunc(
        instance_, "xrCreatePassthroughLayerFB");
    xrDestroyPassthroughLayerFB_ = (PFN_xrDestroyPassthroughLayerFB)LoadLoaderFunc(
        instance_, "xrDestroyPassthroughLayerFB");
    xrPassthroughLayerResumeFB_ = (PFN_xrPassthroughLayerResumeFB)LoadLoaderFunc(
        instance_, "xrPassthroughLayerResumeFB");
    if (!xrCreatePassthroughFB_ || !xrPassthroughStartFB_ ||
        !xrCreatePassthroughLayerFB_ || !xrPassthroughLayerResumeFB_) {
        LOGW("passthrough: entry points missing, disabled");
        return false;
    }
    XrPassthroughCreateInfoFB pci{XR_TYPE_PASSTHROUGH_CREATE_INFO_FB};
    if (xrCreatePassthroughFB_(session_, &pci, &passthrough_) != XR_SUCCESS) {
        LOGW("passthrough: create failed");
        return false;
    }
    XrPassthroughLayerCreateInfoFB plci{XR_TYPE_PASSTHROUGH_LAYER_CREATE_INFO_FB};
    plci.passthrough = passthrough_;
    plci.purpose = XR_PASSTHROUGH_LAYER_PURPOSE_RECONSTRUCTION_FB;
    if (xrCreatePassthroughLayerFB_(session_, &plci, &passthroughLayer_) != XR_SUCCESS) {
        LOGW("passthrough: layer create failed");
        return false;
    }
    xrPassthroughStartFB_(passthrough_);
    xrPassthroughLayerResumeFB_(passthroughLayer_);
    LOGI("passthrough: running");
    return true;
}

void XrApp::StartNetwork() {
    netThread_ = std::thread([this]() {
        auto& cfg = Config::Instance();
        // v0.5.0: check for manual IP override first.
        std::string override = cfg.GetString(ConfigKey::PhoneIpOverride);
        int timeoutMs = cfg.GetInt(ConfigKey::DiscoveryTimeoutMs);
        if (!override.empty()) {
            phoneIp_ = override;
            LOGI("v0.5.0: using manual phone IP %s", phoneIp_.c_str());
            Notify::Instance().Info("Connecting to phone",
                                    ("Manual IP: " + phoneIp_).c_str());
        } else {
            Notify::Instance().Info("Searching for phone",
                                    "Make sure phone is on same WiFi");
            phoneIp_ = DiscoverPhone(timeoutMs);
        }

        if (phoneIp_.empty()) {
            // v0.5.0: human-readable error, not silent failure.
            LOGW("network: phone discovery failed");
            Notify::Instance().Error(
                "Phone not found",
                "Check WiFi, tap Start on phone app");
            return;
        }

        Notify::Instance().Info("Phone found", phoneIp_.c_str());

        // Control channel first (touch input).
        controlClient_.Start(phoneIp_);

        // Video: handshake to learn dimensions, then decoder + stream.
        int w = 720, h = 1280;
        // Temporary client for the handshake; VideoClient::Start does its own.
        if (!videoClient_.Start(phoneIp_, &decoder_, &w, &h)) {
            LOGW("network: video client failed");
            Notify::Instance().Error(
                "Video connection failed",
                "Phone found but video refused. Restart phone app.");
            return;
        }
        videoW_ = w;
        videoH_ = h;

        // Decoder was already configured at Init with a placeholder; if the
        // real dimensions differ we restart it here. (Init starts it lazily
        // on first frame instead — see Frame().)
        videoReady_ = true;
        LOGI("network: phone=%s video=%dx%d", phoneIp_.c_str(), w, h);
        Notify::Instance().Info("Streaming started",
                                "Gaze at your wrist to engage");

        // Ask the phone to turn its screen off while mirroring.
        controlClient_.SendCmd("screen_off");
        screenOffSent_ = true;
    });
    netThread_.detach();
}

bool XrApp::Init(android_app* app) {
    app_ = app;
    // v0.5.0: Load production configuration first — every tunable lives
    // here, no hardcoded values in feature code.
    {
        auto& cfg = Config::Instance();
        // App-private storage for the config file.
        const char* filesDir = app->activity->internalDataPath;
        std::string cfgPath = std::string(filesDir) + "/xrwrist.cfg";
        cfg.SetPath(cfgPath);
        cfg.Load(cfgPath);
        LOGI("v0.5.0: config loaded from %s", cfgPath.c_str());
        // First-run: show onboarding.
        if (!cfg.GetBool(ConfigKey::FirstRunComplete)) {
            Notify::Instance().Info(
                "Welcome to XR Wrist Display",
                "Gaze at your wrist to engage. Tap B for help.");
            // Don't mark complete yet — wait until they've engaged once.
        }
    }
    // Order matters: the XR instance must exist before EGL so we can query
    // the runtime's GL ES version requirements and build a matching context.
    if (!InitXrLoader()) return false;
    if (!InitXrInstance()) return false;
    if (!InitEgl()) return false;

    // Video texture (external OES) + SurfaceTexture on the GL thread.
    glGenTextures(1, &videoTex_);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, videoTex_);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    ANativeWindow* win =
        surfaceTexture_.Create(app_->activity->vm, videoTex_);
    if (!win) {
        LOGE("SurfaceTexture creation failed");
        return false;
    }

    if (!quadRenderer_.Init()) return false;
    avatarReady_ = avatar_.Init();
    if (!avatarReady_) {
        LOGW("avatar init failed; wrist mode falls back to billboard quad");
    }
    if (!diagOverlay_.Init()) {
        LOGW("devtools: diagnostics overlay init failed (non-fatal)");
    }
    // v0.5.0: notification toasts for human-readable errors.
    if (!notifyOverlay_.Init()) {
        LOGW("v0.5.0: notify overlay init failed (non-fatal)");
    }
    if (!InitXrSession()) return false;
    if (!InitSwapchains()) return false;
    // XR dev tools: one-time device capability snapshot (logged; the live
    // values feed the diagnostics overlay).
    {
        std::vector<const char*> exts;
        exts.push_back(kExtGlEs);
        if (passthroughSupported_) exts.push_back(kExtPassthrough);
        if (handTrackingSupported_) exts.push_back(kExtHandTracking);
        if (refreshRateSupported_) exts.push_back(kExtRefreshRate);
        int vw = swapchains_.empty() ? 0 : swapchains_[0].width;
        int vh = swapchains_.empty() ? 0 : swapchains_[0].height;
        deviceInfo_ = devtools::CollectDeviceInfo(
            instance_, systemId_, session_, exts, viewCount_, vw, vh,
            handTrackingSupported_, passthroughSupported_, handMeshSupported_,
            handAimSupported_, refreshRateSupported_);
        LOGW("devtools device info:\n%s", deviceInfo_.ToString().c_str());
    }
    InitPassthrough();  // optional
    if (!input_.Init(instance_, session_, handTrackingSupported_)) {
        LOGW("input init failed (non-fatal)");
    }
    // v0.4.0: voice control bridge (non-fatal if unavailable).
    voice_ = new VoiceBridge();
    if (!voice_->Init(app_->activity->vm, app_->activity->clazz)) {
        LOGW("voice bridge init failed (non-fatal)");
    }
    if (!voiceOverlay_.Init()) {
        LOGW("voice overlay init failed (non-fatal)");
    }

    // Kick off networking; decoder starts once handshake completes.
    // We start the decoder lazily in Frame() when videoReady_ flips.
    decoderWindow_ = win;
    StartNetwork();
    LOGI("XrApp init complete");
    return true;
}

void XrApp::Shutdown() {
    if (screenOffSent_) controlClient_.SendCmd("screen_on");
    delete voice_;
    voice_ = nullptr;
    controlClient_.Stop();
    videoClient_.Stop();
    decoder_.Stop();
    surfaceTexture_.Release();
    input_.Shutdown();
    quadRenderer_.Shutdown();
    if (videoTex_) {
        glDeleteTextures(1, &videoTex_);
        videoTex_ = 0;
    }
    if (passthroughLayer_ != XR_NULL_HANDLE && xrDestroyPassthroughLayerFB_) {
        xrDestroyPassthroughLayerFB_(passthroughLayer_);
        passthroughLayer_ = XR_NULL_HANDLE;
    }
    if (passthrough_ != XR_NULL_HANDLE && xrDestroyPassthroughFB_) {
        if (xrPassthroughPauseFB_) xrPassthroughPauseFB_(passthrough_);
        xrDestroyPassthroughFB_(passthrough_);
        passthrough_ = XR_NULL_HANDLE;
    }
    for (auto& sc : swapchains_) {
        if (!sc.fbos.empty()) {
            glDeleteFramebuffers((GLsizei)sc.fbos.size(), sc.fbos.data());
            sc.fbos.clear();
        }
        if (sc.depthRbo) {
            glDeleteRenderbuffers(1, &sc.depthRbo);
            sc.depthRbo = 0;
        }
        if (sc.handle != XR_NULL_HANDLE) {
            xrDestroySwapchain(sc.handle);
            sc.handle = XR_NULL_HANDLE;
        }
    }
    swapchains_.clear();
    if (localSpace_ != XR_NULL_HANDLE) {
        xrDestroySpace(localSpace_);
        localSpace_ = XR_NULL_HANDLE;
    }
    if (viewSpace_ != XR_NULL_HANDLE) {
        xrDestroySpace(viewSpace_);
        viewSpace_ = XR_NULL_HANDLE;
    }
    if (session_ != XR_NULL_HANDLE) {
        xrDestroySession(session_);
        session_ = XR_NULL_HANDLE;
    }
    if (instance_ != XR_NULL_HANDLE) {
        xrDestroyInstance(instance_);
        instance_ = XR_NULL_HANDLE;
    }
    if (eglDisplay_ != EGL_NO_DISPLAY) {
        eglMakeCurrent(eglDisplay_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (eglContext_ != EGL_NO_CONTEXT) eglDestroyContext(eglDisplay_, eglContext_);
        if (eglPbuffer_ != EGL_NO_SURFACE) eglDestroySurface(eglDisplay_, eglPbuffer_);
        eglTerminate(eglDisplay_);
        eglDisplay_ = EGL_NO_DISPLAY;
    }
}

void XrApp::HandleSessionState(XrSessionState state, bool& exitRequested) {
    sessionState_ = state;
    switch (state) {
        case XR_SESSION_STATE_READY: {
            XrSessionBeginInfo bi{XR_TYPE_SESSION_BEGIN_INFO};
            bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
            if (xrBeginSession(session_, &bi) == XR_SUCCESS) {
                sessionRunning_ = true;
                sessionBegun_ = true;
                LOGI("session running");
            }
            break;
        }
        case XR_SESSION_STATE_STOPPING:
            sessionRunning_ = false;
            if (sessionBegun_) {
                xrEndSession(session_);
                sessionBegun_ = false;
            }
            LOGI("session stopped");
            break;
        case XR_SESSION_STATE_EXITING:
            // VRC: quit cleanly when the runtime asks us to exit.
            sessionRunning_ = false;
            if (sessionBegun_) {
                xrEndSession(session_);
                sessionBegun_ = false;
            }
            LOGI("session exiting; requesting app exit");
            exitRequested = true;
            break;
        case XR_SESSION_STATE_LOSS_PENDING:
            sessionRunning_ = false;
            LOGI("session loss pending; requesting app exit");
            exitRequested = true;
            break;
        case XR_SESSION_STATE_VISIBLE:
            LOGI("session visible (not focused)");
            break;
        case XR_SESSION_STATE_FOCUSED:
            LOGI("session focused");
            break;
        default:
            break;
    }
}

void XrApp::PollEvents(bool& exitRequested) {
    // XR events.
    XrEventDataBuffer buf{XR_TYPE_EVENT_DATA_BUFFER};
    while (xrPollEvent(instance_, &buf) == XR_SUCCESS) {
        switch (buf.type) {
            case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
                auto* e = (XrEventDataSessionStateChanged*)&buf;
                if (e->session == session_) HandleSessionState(e->state, exitRequested);
                break;
            }
            case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
                exitRequested = true;
                break;
            default:
                break;
        }
        buf = {XR_TYPE_EVENT_DATA_BUFFER};
    }
}

void XrApp::ComputeQuadPose(const XrInput::Pose& head, Vec3& outPos,
                            Quat& outQuat, float& outW, float& outH) {
    float aspect = videoH_ > 0 ? (float)videoH_ / (float)videoW_ : 16.0f / 9.0f;
    outW = 0.26f;
    outH = outW * aspect;
    if (outH > 0.6f) {  // clamp very tall screens
        outH = 0.6f;
        outW = outH / aspect;
    }

    Vec3 headPos = head.pos;
    if (anchorMode_ == AnchorMode::WRIST) {
        XrInput::Pose wrist = input_.LeftWrist();
        if (!wrist.valid) wrist = input_.LeftGrip();
        if (wrist.valid) {
            outPos = wrist.pos + Vec3(0.0f, 0.07f, 0.0f);
        } else {
            // Fallback: in front of head.
            Vec3 fwd = head.quat.rotate(Vec3(0, 0, -1));
            outPos = headPos + fwd * 0.6f + Vec3(0, -0.1f, 0);
        }
    } else {
        auto& cfg = Config::Instance();
        float dist = cfg.GetFloat(ConfigKey::WorldDistanceM);
        float heightOff = cfg.GetFloat(ConfigKey::WorldHeightOffsetM);
        Vec3 fwd = head.quat.rotate(Vec3(0, 0, -1));
        outPos = headPos + fwd * dist + Vec3(0, heightOff, 0);
    }

    // Billboard: local +Z faces the head.
    outQuat = BillboardQuat(headPos, outPos);
}

void XrApp::ComputeDiagPanelPose(const XrInput::Pose& head, Vec3& outPos,
                                 Quat& outQuat) {
    // Head-locked, front-left-below: readable without blocking the watch.
    Vec3 fwd = head.quat.rotate(Vec3(0, 0, -1));
    Vec3 up = head.quat.rotate(Vec3(0, 1, 0));
    Vec3 right = head.quat.rotate(Vec3(1, 0, 0));
    outPos = head.pos + fwd * 0.85f + right * (-0.30f) + up * (-0.20f);
    outQuat = BillboardQuat(head.pos, outPos);
}

void XrApp::BuildDiagContent() {
    auto& o = diagOverlay_;
    o.Clear();
    o.Printf("XR WRIST v0.5.0 // GODMODE CONSOLE");
    o.Printf("%5.1f fps  %5.2fms  worst %5.2fms",
             frameDiag_.fps(), frameDiag_.frameMsAvg(),
             frameDiag_.frameMsWorst());
    o.Printf("session %d  video %dx%d %s  phone %s", (int)sessionState_,
             videoW_, videoH_, videoReady_ ? "RDY" : "...",
             phoneIp_.empty() ? "(none)" : phoneIp_.c_str());
    // v0.5.0: hand interaction status.
    o.Printf("hand-interaction %s  poke %s  pinch L:%.2f R:%.2f",
             input_.HandInteractionAvailable() ? "ON" : "off",
             input_.HandInteractionAvailable() ? "canonical" : "joints",
             input_.PinchValue(false), input_.PinchValue(true));
    std::string is;
    devtools::InputSnapshot::Capture(input_).AppendTo(is);
    // AppendTo emits 2 lines; feed them through Printf one by one.
    size_t start = 0;
    while (start < is.size()) {
        size_t e = is.find('\n', start);
        std::string line = is.substr(start, e == std::string::npos
                                                ? std::string::npos
                                                : e - start);
        o.Printf("%s", line.c_str());
        if (e == std::string::npos) break;
        start = e + 1;
    }
    o.Printf("avatar draws %u tris %u dbg %d", lastAvatarStats_.drawCalls,
             lastAvatarStats_.triangles, (int)avatar_.GetDebugMode());
    // v0.5.0 state (SG-1: world-lock on engage).
    const char* zoomName = zoom_ == DisplayZoom::GLANCE ? "glance"
        : zoom_ == DisplayZoom::EXPANDING ? "expanding"
        : zoom_ == DisplayZoom::ENGAGED ? "engaged" : "shrinking";
    const char* anchorName = engageDetached_ ? "ENGAGE-LOCK"
        : (detachT_ < 0.5f ? "wrist" : "world");
    o.Printf("v0.5.0 zoom %s (%.2f) anchor %s detach %.2f->%.0f",
             zoomName, zoomT_, anchorName, detachT_, detachTarget_);
    o.Printf("auto %d glow %.2f finger %s voice %s",
             autoTransition_ ? 1 : 0, watchGlow_,
             fingerTouchDown_ ? "DOWN" : "up",
             voiceListening_ ? "LISTEN" : "idle");
    if (!lastHeard_.empty())
        o.Printf("heard: %s", lastHeard_.c_str());
    if (netProbe_.ScanDone()) {
        auto hosts = netProbe_.TakeResults();
        o.Printf("lan: %d host(s)", (int)hosts.size());
        for (auto& h : hosts) {
            o.Printf(" %s %lldus", h.ip.c_str(), (long long)h.latencyUs);
            o.Printf("  v:%s c:%s",
                     h.video.ok ? "OK" : h.video.error.c_str(),
                     h.control.ok ? "OK" : h.control.error.c_str());
        }
    } else {
        o.Printf("lan scan...");
    }
    // v0.5.0: Godmode command reference.
    o.Printf("--- COMMANDS ---");
    o.Printf("[B]diag [A]wireframe [X+Y]wrist/world");
    o.Printf("[Y-hold]voice [gaze]engage [pinch]click");
    o.Printf("v0.5.0: engage=world-lock (SG-1)");
    // v0.5.0: live config values (production settings).
    auto& cfg = Config::Instance();
    o.Printf("--- CONFIG ---");
    o.Printf("dwell %.1fs lost %.1fs raise %.1fs",
             cfg.GetFloat(ConfigKey::GazeDwellSec),
             cfg.GetFloat(ConfigKey::GazeLostSec),
             cfg.GetFloat(ConfigKey::WristRaiseSec));
    o.Printf("touch %.0fmm pinch %.2f world %.2fm",
             cfg.GetFloat(ConfigKey::TouchDistanceM) * 1000.0f,
             cfg.GetFloat(ConfigKey::PinchThreshold),
             cfg.GetFloat(ConfigKey::WorldDistanceM));
    o.Printf("auto %d voice %d avatar %d",
             cfg.GetBool(ConfigKey::AutoTransition) ? 1 : 0,
             cfg.GetBool(ConfigKey::VoiceEnabled) ? 1 : 0,
             cfg.GetBool(ConfigKey::AvatarEnabled) ? 1 : 0);
}

void XrApp::HandleTouch(const XrInput::Pose& head, const Vec3& quadPos,
                        const Quat& quadQuat, float quadW, float quadH) {
    if (!controlClient_.IsRunning()) return;
    const auto& aim = input_.RightAim();
    float trig = input_.TriggerValue();

    if (input_.TogglePressed()) {
        // v0.4.0: X+Y now drives the animated detach target; the anchor
        // mode commits when the glide finishes.
        detachTarget_ = (detachTarget_ < 0.5f) ? 1.0f : 0.0f;
        wristRaiseDwell_ = 0.0f;
        watchGlow_ = 0.0f;
        LOGI("v0.4.0: X+Y -> %s (animated)",
             detachTarget_ > 0.5f ? "FIXED" : "WRIST");
    }

    if (!aim.valid) {
        if (touchDown_) {
            controlClient_.SendTouch("up", lastU_, lastV_);
            touchDown_ = false;
        }
        return;
    }
    Vec3 dir = aim.quat.rotate(Vec3(0, 0, -1));
    float u = 0, v = 0;
    bool hit = RayQuadIntersect(aim.pos, dir, quadPos, quadQuat, quadW, quadH, u, v);
    bool pressed = trig > 0.5f;

    if (pressed && hit && !touchDown_) {
        controlClient_.SendTouch("down", u, v);
        touchDown_ = true;
        lastU_ = u;
        lastV_ = v;
    } else if (!pressed && touchDown_) {
        controlClient_.SendTouch("up", lastU_, lastV_);
        touchDown_ = false;
    } else if (pressed && touchDown_ && hit) {
        float du = u - lastU_, dv = v - lastV_;
        if (du * du + dv * dv > 1e-6f) {
            controlClient_.SendTouch("move", u, v);
            lastU_ = u;
            lastV_ = v;
        }
    } else if (pressed && touchDown_ && !hit) {
        controlClient_.SendTouch("up", lastU_, lastV_);
        touchDown_ = false;
    }
    (void)head;
}

// v0.4.0: pose for an explicit anchor mode (ComputeQuadPose uses anchorMode_).
void XrApp::ComputeQuadPoseFor(AnchorMode mode, const XrInput::Pose& head,
                               Vec3& outPos, Quat& outQuat,
                               float& outW, float& outH) {
    AnchorMode saved = anchorMode_;
    anchorMode_ = mode;
    ComputeQuadPose(head, outPos, outQuat, outW, outH);
    anchorMode_ = saved;
}

// v0.4.0: dynamic sizing — sustained gaze (>1.5s) expands the display;
// looking away (>2s) shrinks it back. Ease-out-back animation.
//
// v0.5.0 (SG-1): Meta's hand-tracking guidance explicitly warns: "Do NOT
// anchor complex menus to an active, moving wrist." When the display
// engages (user wants to interact), it MUST go world-locked. The wrist is
// for glancing; the world is for doing.
void XrApp::UpdateDynamicZoom(const XrInput::Pose& head, const Vec3& dispPos,
                              const Quat& dispQuat, float dispW, float dispH,
                              float dt) {
    bool gazeHit = false;
    if (head.valid) {
        Vec3 fwd = head.quat.rotate(Vec3(0, 0, -1));
        float u, v;
        gazeHit = RayQuadIntersect(head.pos, fwd, dispPos, dispQuat, dispW,
                                   dispH, u, v);
    }
    auto& cfg = Config::Instance();
    float dwellThreshold = cfg.GetFloat(ConfigKey::GazeDwellSec);
    float lostThreshold = cfg.GetFloat(ConfigKey::GazeLostSec);
    switch (zoom_) {
        case DisplayZoom::GLANCE:
            if (gazeHit) {
                gazeDwell_ += dt;
                if (gazeDwell_ > dwellThreshold) {
                    zoom_ = DisplayZoom::EXPANDING;
                    LOGI("v0.5.0: display expanding (gaze dwell)");
                    // v0.5.0: first-run onboarding complete — user figured
                    // out the core interaction.
                    if (!cfg.GetBool(ConfigKey::FirstRunComplete)) {
                        cfg.SetBool(ConfigKey::FirstRunComplete, true);
                        Notify::Instance().Info(
                            "You're in!",
                            "Pinch to click. Hold Y for voice. B for help.");
                    }
                    // SG-1: engaging means interacting — the panel must be
                    // world-locked, not wrist-anchored. Start the glide now
                    // so it's stable by the time EXPANDING finishes.
                    if (detachTarget_ < 0.5f) {
                        detachTarget_ = 1.0f;
                        engageDetached_ = true;
                        wristRaiseDwell_ = 0.0f;
                        watchGlow_ = 0.0f;
                        LOGI("v0.5.0: engage -> world-lock (SG-1)");
                        Notify::Instance().Info(
                            "Display engaged",
                            "Panel moved to world-locked position");
                    }
                }
            } else {
                gazeDwell_ = 0.0f;
            }
            break;
        case DisplayZoom::EXPANDING:
            zoomT_ += dt / 0.4f;
            if (zoomT_ >= 1.0f) {
                zoomT_ = 1.0f;
                zoom_ = DisplayZoom::ENGAGED;
                gazeLost_ = 0.0f;
            }
            break;
        case DisplayZoom::ENGAGED:
            if (gazeHit) {
                gazeLost_ = 0.0f;
            } else {
                gazeLost_ += dt;
                if (gazeLost_ > lostThreshold) zoom_ = DisplayZoom::SHRINKING;
            }
            break;
        case DisplayZoom::SHRINKING:
            zoomT_ -= dt / 0.4f;
            if (zoomT_ <= 0.0f) {
                zoomT_ = 0.0f;
                zoom_ = DisplayZoom::GLANCE;
                gazeDwell_ = 0.0f;
                // SG-1: if engage caused the detach, return to wrist now.
                // If the user manually detached (X+Y) or auto-transition
                // did it, leave the panel where it is.
                if (engageDetached_) {
                    engageDetached_ = false;
                    detachTarget_ = 0.0f;
                    LOGI("v0.5.0: disengage -> return to wrist (SG-1)");
                }
            }
            break;
    }
}

// v0.4.0: posture-driven auto wrist->fixed. Holding the wrist in reading
// posture (>4s) detaches the display with a 600ms eased glide; the bezel
// glows amber during the final second as a pre-detach warning. Lowering
// the arm and raising it again re-attaches.
void XrApp::UpdateAutoTransition(const XrInput::Pose& head, float dt) {
    if (!head.valid) {
        watchGlow_ = 0.0f;
        return;
    }
    XrInput::Pose wrist = input_.LeftWrist();
    if (!wrist.valid) wrist = input_.LeftGrip();
    float relY = wrist.valid ? (wrist.pos.y - head.pos.y) : -1.0f;
    bool raised = wrist.valid && relY > -0.15f;
    bool lowered = !wrist.valid || relY < -0.35f;

    auto& cfg = Config::Instance();
    float raiseThreshold = cfg.GetFloat(ConfigKey::WristRaiseSec);
    bool autoTrans = cfg.GetBool(ConfigKey::AutoTransition);
    if (autoTrans && anchorMode_ == AnchorMode::WRIST &&
        detachTarget_ == 0.0f) {
        if (raised) {
            wristRaiseDwell_ += dt;
            // Amber pre-detach warning during the last second.
            watchGlow_ = Clamp01((wristRaiseDwell_ - (raiseThreshold - 1.0f)) / 1.0f)
                * cfg.GetFloat(ConfigKey::WatchGlowIntensity);
            if (wristRaiseDwell_ > raiseThreshold) {
                detachTarget_ = 1.0f;
                watchGlow_ = 0.0f;
                if (!detachHintShown_) {
                    detachHintShown_ = true;
                    LOGW("v0.4.0: display auto-detached "
                         "(raise wrist to bring it back)");
                } else {
                    LOGI("v0.4.0: display auto-detached");
                }
            }
        } else {
            wristRaiseDwell_ = 0.0f;
            watchGlow_ = 0.0f;
        }
    } else if (autoTrans && anchorMode_ == AnchorMode::FIXED &&
               detachTarget_ == 1.0f) {
        watchGlow_ = 0.0f;
        if (lowered) wristWasLowered_ = true;
        if (wristWasLowered_ && raised) {
            detachTarget_ = 0.0f;
            wristWasLowered_ = false;
            wristRaiseDwell_ = 0.0f;
            LOGI("v0.4.0: display re-attached to wrist");
        }
    } else {
        watchGlow_ = 0.0f;
    }

    // Animate the blend; commit the anchor mode at the end of travel.
    const float kDetachTime = 0.6f;
    if (detachT_ < detachTarget_) {
        detachT_ = fminf(detachT_ + dt / kDetachTime, detachTarget_);
        if (detachT_ >= 1.0f && anchorMode_ == AnchorMode::WRIST) {
            anchorMode_ = AnchorMode::FIXED;
            LOGI("v0.4.0: anchor mode -> FIXED");
        }
    } else if (detachT_ > detachTarget_) {
        detachT_ = fmaxf(detachT_ - dt / kDetachTime, detachTarget_);
        if (detachT_ <= 0.0f && anchorMode_ == AnchorMode::FIXED) {
            anchorMode_ = AnchorMode::WRIST;
            LOGI("v0.4.0: anchor mode -> WRIST");
        }
    }
}

// v0.4.0: direct finger touch — the tracked index fingertip becomes the
// cursor. Contact within 3cm of the display surface registers a tap.
//
// v0.5.0 (SG-4): prefer the canonical XR_EXT_hand_interaction poke pose
// when available (runtime-tuned). Fall back to raw joint fingertip.
void XrApp::HandleFingerTouch(const Vec3& quadPos, const Quat& quadQuat,
                              float quadW, float quadH) {
    if (!controlClient_.IsRunning()) {
        fingerTouchDown_ = false;
        return;
    }
    auto& cfg = Config::Instance();
    float touchDist = cfg.GetFloat(ConfigKey::TouchDistanceM);
    float pinchThresh = cfg.GetFloat(ConfigKey::PinchThreshold);
    bool usePoke = input_.HandInteractionAvailable();
    bool anyTouch = false;
    float bestU = 0.0f, bestV = 0.0f;
    for (int h = 0; h < 2; ++h) {
        bool right = (h == 1);
        XrInput::Pose tip = usePoke ? input_.PokePose(right)
                                    : input_.IndexTip(right);
        if (!tip.valid) continue;
        float u, v, dist;
        if (!FingerQuadIntersect(tip.pos, quadPos, quadQuat, quadW, quadH,
                                 u, v, dist))
            continue;
        if (fabsf(dist) < touchDist) {
            anyTouch = true;
            bestU = u;
            bestV = v;
            break;
        }
    }
    if (usePoke && !anyTouch) {
        // SG-4: also check pinch as a click alternative when poke misses
        // but the user is pinching near the panel (mid-air selection).
        for (int h = 0; h < 2; ++h) {
            bool right = (h == 1);
            if (input_.PinchValue(right) > pinchThresh) {
                XrInput::Pose poke = input_.PokePose(right);
                if (!poke.valid) continue;
                float u, v, dist;
                if (FingerQuadIntersect(poke.pos, quadPos, quadQuat, quadW,
                                        quadH, u, v, dist) &&
                    fabsf(dist) < 0.15f) {
                    anyTouch = true;
                    bestU = u;
                    bestV = v;
                    break;
                }
            }
        }
    }
    if (anyTouch && !fingerTouchDown_) {
        controlClient_.SendTouch("down", bestU, bestV);
        fingerTouchDown_ = true;
        fingerU_ = bestU;
        fingerV_ = bestV;
        touchPulseT_ = 0.0f;  // start the ripple visual
        touchPulseU_ = bestU;
        touchPulseV_ = bestV;
    } else if (!anyTouch && fingerTouchDown_) {
        controlClient_.SendTouch("up", fingerU_, fingerV_);
        fingerTouchDown_ = false;
    } else if (anyTouch && fingerTouchDown_) {
        float du = bestU - fingerU_, dv = bestV - fingerV_;
        if (du * du + dv * dv > 1e-6f) {
            controlClient_.SendTouch("move", bestU, bestV);
            fingerU_ = bestU;
            fingerV_ = bestV;
        }
    }
}

static std::string ToLowerStr(std::string s) {
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

// v0.4.0: tiny command grammar over the recognized utterance.
void XrApp::ExecuteVoiceCommand(const std::string& cmd) {
    std::string c = ToLowerStr(cmd);
    auto has = [&](const char* w) { return c.find(w) != std::string::npos; };
    if (has("expand") || has("bigger") || has("zoom in")) {
        if (zoom_ == DisplayZoom::GLANCE) zoom_ = DisplayZoom::EXPANDING;
        LOGW("v0.4.0: voice -> expand");
    } else if (has("shrink") || has("small") || has("zoom out") ||
               has("collapse")) {
        if (zoom_ == DisplayZoom::ENGAGED) zoom_ = DisplayZoom::SHRINKING;
        LOGW("v0.4.0: voice -> shrink");
    } else if (has("wrist")) {
        detachTarget_ = 0.0f;
        LOGW("v0.4.0: voice -> wrist mode");
    } else if (has("fix") || has("float") || has("detach") ||
               has("move here")) {
        detachTarget_ = 1.0f;
        LOGW("v0.4.0: voice -> fixed mode");
    } else if (has("auto move on") || has("auto on")) {
        autoTransition_ = true;
        LOGW("v0.4.0: voice -> auto-transition on");
    } else if (has("auto move off") || has("auto off")) {
        autoTransition_ = false;
        LOGW("v0.4.0: voice -> auto-transition off");
    } else if (has("diagnostics") || has("godmode") || has("debug info")) {
        // v0.5.0: toggle the Godmode console via voice.
        showDiag_ = !showDiag_;
        if (showDiag_) netProbe_.StartScan(1500);
        LOGW("v0.5.0: voice -> godmode %s", showDiag_ ? "on" : "off");
    } else if (has("world lock") || has("lock here")) {
        // v0.5.0: force world-lock at the current panel position.
        detachTarget_ = 1.0f;
        engageDetached_ = false;  // manual, not engage-caused
        LOGW("v0.5.0: voice -> world lock");
    } else {
        LOGW("v0.5.0: voice command not recognized: \"%s\"", cmd.c_str());
    }
}

// v0.4.0: push-to-talk — hold Y 0.8s to listen, then match the utterance.
void XrApp::UpdateVoice(float dt) {
    // v0.5.0: voice can be disabled via Config.
    if (!Config::Instance().GetBool(ConfigKey::VoiceEnabled)) {
        voiceListening_ = false;
        voiceHoldT_ = 0.0f;
        return;
    }
    if (input_.YDown()) {
        voiceHoldT_ += dt;
        if (voiceHoldT_ > 0.8f && !voiceListening_ && voice_) {
            voice_->StartListening();
            voiceListening_ = true;
            voiceHoldT_ = 0.0f;
            LOGW("v0.4.0: voice listening... (speak a command)");
        }
    } else {
        voiceHoldT_ = 0.0f;
    }
    if (voice_ && voiceListening_) {
        bool listening = false;
        std::string heard = voice_->PollResult(listening);
        voiceListening_ = listening;
        if (!heard.empty()) {
            lastHeard_ = heard;
            lastHeardT_ = (float)frameCount_;
            LOGW("v0.4.0: heard \"%s\"", heard.c_str());
            ExecuteVoiceCommand(heard);
        }
    }
}

void XrApp::RenderLayer(XrTime predictedTime) {
    // Locate views.
    XrViewLocateInfo viewInfo{XR_TYPE_VIEW_LOCATE_INFO};
    viewInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    viewInfo.displayTime = predictedTime;
    viewInfo.space = localSpace_;
    XrViewState viewState{XR_TYPE_VIEW_STATE};
    uint32_t count = 0;
    if (xrLocateViews(session_, &viewInfo, &viewState, viewCount_, &count,
                      views_.data()) != XR_SUCCESS) {
        LOGW("xrLocateViews failed; ending frame with no layers");
        XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
        endInfo.displayTime = predictedTime;
        endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND;
        endInfo.layerCount = 0;
        xrEndFrame(session_, &endInfo);
        lastAvatarStats_ = avatar_.TakeRenderStats();
        return;
    }

    // Head pose for billboard + fixed anchor.
    XrSpaceLocation headLoc{XR_TYPE_SPACE_LOCATION};
    xrLocateSpace(viewSpace_, localSpace_, predictedTime, &headLoc);
    XrInput::Pose head;
    if ((headLoc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) &&
        (headLoc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) {
        head.valid = true;
        head.pos = {headLoc.pose.position.x, headLoc.pose.position.y,
                    headLoc.pose.position.z};
        head.quat = {headLoc.pose.orientation.x, headLoc.pose.orientation.y,
                     headLoc.pose.orientation.z, headLoc.pose.orientation.w};
    }
    input_.SetHead(head);

    // Start decoder lazily once the handshake gave us dimensions.
    if (videoReady_ && !decoder_.IsRunning() && decoderWindow_) {
        decoder_.Start(decoderWindow_, videoW_, videoH_);
    }

    // New video frame -> update the external texture.
    surfaceTexture_.Update();

    // ---- v0.4.0: per-frame dt from predicted display time ----
    float dt = 1.0f / 72.0f;
    if (lastPredictedTime_ != 0 && predictedTime > lastPredictedTime_) {
        dt = (float)(predictedTime - lastPredictedTime_) / 1e9f;
        if (dt <= 0.0f || dt > 0.25f) dt = 1.0f / 72.0f;
    }
    lastPredictedTime_ = predictedTime;

    // v0.4.0: posture-driven auto wrist->fixed (updates detachT_/anchorMode_).
    UpdateAutoTransition(head, dt);
    // v0.4.0: push-to-talk voice control.
    UpdateVoice(dt);

    // Display pose + touch. The smartwatch renders on the LEFT wrist in
    // WRIST mode; both avatar hands render whenever their controllers are
    // tracked. Otherwise the billboard quad is used.
    //
    // v0.4.0: wrist and fixed poses are blended by detachT_ so the
    // auto-transition glides instead of popping.
    Vec3 dispPos;
    Quat dispQuat;
    float dispW, dispH;
    XrInput::Pose gripLeft = input_.LeftGrip();
    XrInput::Pose gripRight = input_.RightGrip();
    // v0.5.0: avatar can be disabled via Config (e.g. for performance).
    bool avatarOn = Config::Instance().GetBool(ConfigKey::AvatarEnabled);
    bool showWatch = avatarReady_ && avatarOn && gripLeft.valid && detachT_ < 0.5f;
    bool showHands =
        avatarReady_ && avatarOn && (gripLeft.valid || gripRight.valid);
    Vec3 headPos = head.valid ? head.pos : Vec3(0, 0, 0);

    Vec3 wristPos, fixedPos;
    Quat wristQuat, fixedQuat;
    float wristW, wristH, fixedW, fixedH;
    if (showWatch || (avatarReady_ && gripLeft.valid)) {
        AvatarRenderer::ComputeWatchPose(gripLeft, headPos, wristPos,
                                        wristQuat);
        wristW = avatar_dims::kWatchFaceW;
        wristH = avatar_dims::kWatchFaceH;
    } else {
        ComputeQuadPoseFor(AnchorMode::WRIST, head, wristPos, wristQuat,
                           wristW, wristH);
    }
    ComputeQuadPoseFor(AnchorMode::FIXED, head, fixedPos, fixedQuat,
                       fixedW, fixedH);
    float bt = detachT_ * detachT_ * (3.0f - 2.0f * detachT_);  // smoothstep
    dispPos = wristPos * (1.0f - bt) + fixedPos * bt;
    dispQuat = Nlerp(wristQuat, fixedQuat, bt);
    dispW = wristW * (1.0f - bt) + fixedW * bt;
    dispH = wristH * (1.0f - bt) + fixedH * bt;

    // v0.4.0: dynamic sizing — gaze dwell expands the display.
    // v0.5.0 (SG-1): when ENGAGED, the panel is world-locked (bt->1).
    // Scale it for comfortable reading at 0.7m: target ~0.42m wide
    // (Meta's 1024dp default panel at typical density). When on wrist,
    // keep the watch-face scaling for the glance->expand feel.
    UpdateDynamicZoom(head, dispPos, dispQuat, dispW, dispH, dt);
    float zoomScale = 1.0f + EaseOutBack(zoomT_) * 1.4f;  // 1.0x -> 2.4x
    if (bt < 0.5f) {
        // Wrist-anchored: scale the watch face.
        dispW *= zoomScale;
        dispH *= zoomScale;
        avatar_.SetWatchScale(zoomScale);
    } else {
        // World-locked: scale toward comfortable reading size.
        // Base fixed size is 0.26m; engaged target is ~0.42m (1.6x).
        float worldScale = 1.0f + EaseOutBack(zoomT_) * 0.6f;  // 1.0x -> 1.6x
        dispW *= worldScale;
        dispH *= worldScale;
        avatar_.SetWatchScale(1.0f);  // watch stays normal size on wrist
    }
    avatar_.SetWatchGlow(watchGlow_);
    // Touch ripple decay.
    touchPulseT_ += dt;
    avatar_.SetTouchPulse(touchPulseU_, touchPulseV_,
                          touchPulseT_ < 0.5f ? 1.0f - touchPulseT_ / 0.5f
                                              : 0.0f);

    HandleTouch(head, dispPos, dispQuat, dispW, dispH);
    // v0.4.0: direct finger touch takes priority over the controller ray.
    HandleFingerTouch(dispPos, dispQuat, dispW, dispH);

    // GODMODE: A cycles the avatar debug visualization (off/wire/normals).
    if (input_.APressed() && avatarReady_) {
        auto m = avatar_.GetDebugMode();
        auto next =
            (m == AvatarRenderer::DebugMode::Off)
                ? AvatarRenderer::DebugMode::Wireframe
            : (m == AvatarRenderer::DebugMode::Wireframe)
                ? AvatarRenderer::DebugMode::Normals
                : AvatarRenderer::DebugMode::Off;
        avatar_.SetDebugMode(next);
        LOGW("godmode: avatar debug mode = %d", (int)next);
    }
    // B toggles the diagnostics overlay (starts a LAN scan when opened).
    if (input_.BPressed()) {
        showDiag_ = !showDiag_;
        if (showDiag_) netProbe_.StartScan(1500);
        LOGW("godmode: diagnostics overlay %s", showDiag_ ? "on" : "off");
    }
    if (showDiag_ && head.valid) BuildDiagContent();

    // Tints: slightly blue in FIXED mode so the mode is visible.
    float tint[4] = {1, 1, 1, 1};
    if (anchorMode_ == AnchorMode::FIXED) {
        tint[0] = 0.85f;
        tint[1] = 0.95f;
        tint[2] = 1.0f;
    }

    std::vector<XrCompositionLayerBaseHeader*> layers;
    XrCompositionLayerPassthroughFB pl{XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_FB};
    if (passthroughLayer_ != XR_NULL_HANDLE) {
        pl.layerHandle = passthroughLayer_;
        layers.push_back((XrCompositionLayerBaseHeader*)&pl);
    }

    std::vector<XrCompositionLayerProjectionView> projViews;
    projViews.reserve(viewCount_);
    for (uint32_t i = 0; i < viewCount_; ++i) {
        auto& sc = swapchains_[i];
        // Acquire + wait image. VRC: bounded wait, never INFINITE — a wedged
        // compositor must not stall the frame loop forever.
        uint32_t imgIdx = 0;
        XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        if (xrAcquireSwapchainImage(sc.handle, &acquire, &imgIdx) != XR_SUCCESS) {
            LOGW("xrAcquireSwapchainImage failed for view %u; skipping", i);
            continue;
        }
        XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        wait.timeout = 100 * 1000 * 1000;  // 100 ms
        XrResult wr = xrWaitSwapchainImage(sc.handle, &wait);
        if (wr != XR_SUCCESS) {
            LOGW("xrWaitSwapchainImage failed for view %u (%d); releasing",
                 i, (int)wr);
            XrSwapchainImageReleaseInfo rel{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            xrReleaseSwapchainImage(sc.handle, &rel);
            continue;
        }

        // View/projection matrices.
        const XrPosef& vp = views_[i].pose;
        Vec3 vpos{vp.position.x, vp.position.y, vp.position.z};
        Quat vquat{vp.orientation.x, vp.orientation.y, vp.orientation.z,
                   vp.orientation.w};
        Mat4 viewMat = Mat4::FromPose(vpos, vquat).invertedRigid();
        const XrFovf& fov = views_[i].fov;
        Mat4 projMat = Mat4::FromXrFov(fov.angleLeft, fov.angleRight,
                                       fov.angleUp, fov.angleDown, 0.05f, 100.0f);
        Mat4 viewProj = projMat * viewMat;

        const XrSwapchainImageOpenGLESKHR& img = sc.images[imgIdx];
        (void)img;
        glBindFramebuffer(GL_FRAMEBUFFER, sc.fbos[imgIdx]);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                  GL_RENDERBUFFER, sc.depthRbo);
        glViewport(0, 0, sc.width, sc.height);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (showHands) {
            // Premium path: articulated hands (+ smartwatch on the left
            // wrist), depth-tested.
            glDisable(GL_BLEND);
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LESS);
            glDepthMask(GL_TRUE);
            float sx, sy;
            if (gripLeft.valid) {
                AvatarRenderer::HandPose hp;
                hp.indexCurl = input_.LeftTrigger();
                hp.gripCurl = input_.LeftSqueeze();
                input_.LeftThumbstick(sx, sy);
                hp.thumbAbduct = sx;
                hp.thumbFlex = sy;
                avatar_.DrawHand(viewProj, vpos, gripLeft, hp,
                                 false /* left */);
            }
            if (gripRight.valid) {
                AvatarRenderer::HandPose hp;
                hp.indexCurl = input_.RightTrigger();
                hp.gripCurl = input_.RightSqueeze();
                input_.RightThumbstick(sx, sy);
                hp.thumbAbduct = sx;
                hp.thumbFlex = sy;
                avatar_.DrawHand(viewProj, vpos, gripRight, hp,
                                 true /* mirrored right */);
            }
            if (showWatch) {
                // v0.4.0: wrist-anchored smartwatch (zoom via SetWatchScale).
                avatar_.DrawWatch(viewProj, vpos, dispPos, dispQuat,
                                   videoTex_);
            } else {
                // v0.4.0: detached -> Meta-fluent floating panel.
                avatar_.DrawPanel(viewProj, vpos, dispPos, dispQuat, dispW,
                                  dispH, videoTex_);
            }
            glDisable(GL_DEPTH_TEST);
        } else {
            Mat4 modelMat = Mat4::FromPose(dispPos, dispQuat);
            // Scale quad to dispW x dispH.
            Mat4 scale = Mat4::Identity();
            scale.m[0] = dispW;
            scale.m[5] = dispH;
            Mat4 mvp = viewProj * modelMat * scale;
            quadRenderer_.Draw(sc.fbos[imgIdx], sc.width, sc.height, mvp,
                               videoTex_, tint);
        }

        // Godmode diagnostics overlay (head-locked panel, both eyes).
        if (showDiag_ && head.valid && diagOverlay_.HasContent()) {
            Vec3 panelPos;
            Quat panelQuat;
            ComputeDiagPanelPose(head, panelPos, panelQuat);
            Mat4 panelModel = Mat4::FromPose(panelPos, panelQuat);
            diagOverlay_.Draw(viewProj, panelModel, 0.42f);
        }

        // v0.4.0: voice mic pill while listening (Meta-style indicator).
        if (voiceListening_ && head.valid) {
            voiceOverlay_.Clear();
            voiceOverlay_.Printf("MIC listening - speak command");
            Vec3 fwd = head.quat.rotate(Vec3(0, 0, -1));
            Vec3 up = head.quat.rotate(Vec3(0, 1, 0));
            Vec3 pillPos = head.pos + fwd * 0.9f + up * 0.18f;
            Mat4 pillModel =
                Mat4::FromPose(pillPos, BillboardQuat(head.pos, pillPos));
            voiceOverlay_.Draw(viewProj, pillModel, 0.30f);
        }

        // v0.5.0: user-facing notification toasts (human-readable errors
        // with recovery actions — no silent failures).
        if (head.valid) {
            Notification n;
            // Use frame time as the clock for notification expiry.
            double nowSec = (double)frameCount_ / 72.0;
            if (Notify::Instance().Latest(n, nowSec, 8.0)) {
                notifyOverlay_.Clear();
                const char* prefix = n.level == NotifyLevel::Error ? "[!]"
                    : n.level == NotifyLevel::Warn ? "[*]" : "[i]";
                notifyOverlay_.Printf("%s %s", prefix, n.title.c_str());
                if (!n.action.empty())
                    notifyOverlay_.Printf("  %s", n.action.c_str());
                Vec3 fwd = head.quat.rotate(Vec3(0, 0, -1));
                Vec3 up = head.quat.rotate(Vec3(0, 1, 0));
                // Position below the voice pill, above the control bar area.
                Vec3 toastPos = head.pos + fwd * 0.9f + up * 0.05f;
                Mat4 toastModel = Mat4::FromPose(
                    toastPos, BillboardQuat(head.pos, toastPos));
                notifyOverlay_.Draw(viewProj, toastModel, 0.35f);
            }
        }

        XrSwapchainImageReleaseInfo rel{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        xrReleaseSwapchainImage(sc.handle, &rel);

        XrCompositionLayerProjectionView pv{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
        pv.pose = vp;
        pv.fov = fov;
        XrRect2Di rect{};
        rect.extent.width = sc.width;
        rect.extent.height = sc.height;
        pv.subImage.swapchain = sc.handle;
        pv.subImage.imageRect = rect;
        pv.subImage.imageArrayIndex = 0;
        projViews.push_back(pv);
    }

    if (projViews.empty()) {
        // No swapchain images available this frame; submit empty so the
        // compositor keeps running instead of stalling on a bad layer.
        LOGW("no projection views acquired; ending frame with no layers");
        XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
        endInfo.displayTime = predictedTime;
        endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND;
        endInfo.layerCount = 0;
        xrEndFrame(session_, &endInfo);
        lastAvatarStats_ = avatar_.TakeRenderStats();
        return;
    }

    XrCompositionLayerProjection proj{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    proj.space = localSpace_;
    proj.viewCount = (uint32_t)projViews.size();
    proj.views = projViews.data();
    layers.push_back((XrCompositionLayerBaseHeader*)&proj);

    XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
    endInfo.displayTime = predictedTime;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND;
    endInfo.layerCount = (uint32_t)layers.size();
    endInfo.layers = layers.data();
    XrResult er = xrEndFrame(session_, &endInfo);
    frameCount_++;
    if (frameCount_ % 300 == 1) {
        LOGI("frame %llu submitted (xrEndFrame=%d, layers=%u, videoReady=%d)",
             (unsigned long long)frameCount_, (int)er,
             (unsigned)layers.size(), videoReady_ ? 1 : 0);
    }
    // Stash this frame's avatar stats for the diagnostics overlay.
    lastAvatarStats_ = avatar_.TakeRenderStats();
}

void XrApp::Frame() {
    if (!sessionRunning_) return;
    frameDiag_.BeginFrame();

    XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState frameState{XR_TYPE_FRAME_STATE};
    if (xrWaitFrame(session_, &waitInfo, &frameState) != XR_SUCCESS) {
        frameDiag_.EndFrame();
        return;
    }

    XrFrameBeginInfo beginInfo{XR_TYPE_FRAME_BEGIN_INFO};
    if (xrBeginFrame(session_, &beginInfo) != XR_SUCCESS) {
        frameDiag_.EndFrame();
        return;
    }

    input_.Poll(frameState.predictedDisplayTime, localSpace_);

    if (frameState.shouldRender == XR_TRUE) {
        RenderLayer(frameState.predictedDisplayTime);
    } else {
        XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
        endInfo.displayTime = frameState.predictedDisplayTime;
        endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND;
        endInfo.layerCount = 0;
        xrEndFrame(session_, &endInfo);
        lastAvatarStats_ = avatar_.TakeRenderStats();
    }
    frameDiag_.EndFrame();
}

}  // namespace xrwrist
