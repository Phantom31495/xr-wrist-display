#include "video.h"
#include "log.h"
#include "net.h"

#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <android/native_window_jni.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>

namespace xrwrist {

// ---------------- SurfaceTextureHelper ----------------

JNIEnv* SurfaceTextureHelper::Env() {
    JNIEnv* env = nullptr;
    if (vm_->AttachCurrentThread(&env, nullptr) != JNI_OK) return nullptr;
    return env;
}

ANativeWindow* SurfaceTextureHelper::Create(JavaVM* vm, uint32_t glTexId) {
    vm_ = vm;
    JNIEnv* env = Env();
    if (!env) {
        LOGE("SurfaceTexture: no JNIEnv");
        return nullptr;
    }

    jclass stClass = env->FindClass("android/graphics/SurfaceTexture");
    if (!stClass) {
        LOGE("SurfaceTexture: class not found");
        return nullptr;
    }
    jmethodID ctor = env->GetMethodID(stClass, "<init>", "(I)V");
    updateMid_ = env->GetMethodID(stClass, "updateTexImage", "()V");
    getMatrixMid_ = env->GetMethodID(stClass, "getTransformMatrix", "([F)V");
    jmethodID releaseMid = env->GetMethodID(stClass, "release", "()V");
    (void)releaseMid;
    if (!ctor || !updateMid_ || !getMatrixMid_) {
        LOGE("SurfaceTexture: methods not found");
        return nullptr;
    }

    jobject local = env->NewObject(stClass, ctor, static_cast<jint>(glTexId));
    if (!local || env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        LOGE("SurfaceTexture: ctor failed");
        return nullptr;
    }
    surfaceTextureObj_ = env->NewGlobalRef(local);
    env->DeleteLocalRef(local);
    env->DeleteLocalRef(stClass);

    // Wrap in android.view.Surface -> ANativeWindow for MediaCodec.
    jclass surfClass = env->FindClass("android/view/Surface");
    jmethodID surfCtor =
        env->GetMethodID(surfClass, "<init>", "(Landroid/graphics/SurfaceTexture;)V");
    jobject surfObj = env->NewObject(surfClass, surfCtor, surfaceTextureObj_);
    if (!surfObj || env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        LOGE("SurfaceTexture: Surface ctor failed");
        return nullptr;
    }
    window_ = ANativeWindow_fromSurface(env, surfObj);
    env->DeleteLocalRef(surfObj);
    env->DeleteLocalRef(surfClass);

    if (!window_) {
        LOGE("SurfaceTexture: ANativeWindow_fromSurface failed");
        return nullptr;
    }
    LOGI("SurfaceTexture: created, window=%p", (void*)window_);
    return window_;
}

void SurfaceTextureHelper::Update() {
    if (!surfaceTextureObj_) return;
    JNIEnv* env = Env();
    if (!env) return;
    env->CallVoidMethod(surfaceTextureObj_, updateMid_);
    if (env->ExceptionCheck()) {
        // No new frame yet is NOT an error here; just clear and continue.
        env->ExceptionClear();
    } else {
        // v0.6.2: a new frame arrived — count it for stream health.
        ++frames_;
    }
}

void SurfaceTextureHelper::GetTransformMatrix(float out16[16]) {
    if (!surfaceTextureObj_) return;
    JNIEnv* env = Env();
    if (!env) return;
    jfloatArray arr = env->NewFloatArray(16);
    env->CallFloatMethod(surfaceTextureObj_, getMatrixMid_, arr);
    if (!env->ExceptionCheck()) {
        env->GetFloatArrayRegion(arr, 0, 16, out16);
    } else {
        env->ExceptionClear();
    }
    env->DeleteLocalRef(arr);
}

void SurfaceTextureHelper::Release() {
    if (surfaceTextureObj_) {
        JNIEnv* env = Env();
        if (env) {
            jclass stClass = env->FindClass("android/graphics/SurfaceTexture");
            jmethodID releaseMid = env->GetMethodID(stClass, "release", "()V");
            env->CallVoidMethod(surfaceTextureObj_, releaseMid);
            env->DeleteGlobalRef(surfaceTextureObj_);
            env->DeleteLocalRef(stClass);
        }
        surfaceTextureObj_ = nullptr;
    }
    if (window_) {
        ANativeWindow_release(window_);
        window_ = nullptr;
    }
}

// ---------------- VideoDecoder ----------------

bool VideoDecoder::Start(ANativeWindow* window, int width, int height) {
    if (running_) return true;
    window_ = window;
    running_ = true;
    thread_ = std::thread(&VideoDecoder::DecodeLoop, this, width, height);
    return true;
}

void VideoDecoder::Stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
    std::lock_guard<std::mutex> lock(mutex_);
    while (!queue_.empty()) queue_.pop();
    queueBytes_ = 0;
    window_ = nullptr;
}

void VideoDecoder::FeedNal(const uint8_t* data, size_t len) {
    if (!running_) return;
    std::lock_guard<std::mutex> lock(mutex_);
    // Drop oldest data if we're falling behind (live stream: prefer fresh frames).
    while (queueBytes_ + len > kMaxQueueBytes && !queue_.empty()) {
        queueBytes_ -= queue_.front().size();
        queue_.pop();
    }
    queue_.emplace(data, data + len);
    queueBytes_ += len;
}

void VideoDecoder::DecodeLoop(int width, int height) {
    AMediaCodec* codec = AMediaCodec_createDecoderByType("video/avc");
    if (!codec) {
        LOGE("decoder: createDecoderByType failed");
        running_ = false;
        return;
    }
    AMediaFormat* format = AMediaFormat_new();
    AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, "video/avc");
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, width);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, height);

    media_status_t st =
        AMediaCodec_configure(codec, format, window_, nullptr, 0);
    AMediaFormat_delete(format);
    if (st != AMEDIA_OK) {
        LOGE("decoder: configure failed (%d)", (int)st);
        AMediaCodec_delete(codec);
        running_ = false;
        return;
    }
    if (AMediaCodec_start(codec) != AMEDIA_OK) {
        LOGE("decoder: start failed");
        AMediaCodec_delete(codec);
        running_ = false;
        return;
    }
    LOGI("decoder: started %dx%d", width, height);

    const int64_t kInputTimeoutUs = 10000;
    const int64_t kOutputTimeoutUs = 0;
    int64_t ptsUs = 0;

    while (running_) {
        // --- feed input ---
        ssize_t inIdx = AMediaCodec_dequeueInputBuffer(codec, kInputTimeoutUs);
        if (inIdx >= 0) {
            std::vector<uint8_t> nal;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!queue_.empty()) {
                    nal = std::move(queue_.front());
                    queue_.pop();
                    queueBytes_ -= nal.size();
                }
            }
            if (!nal.empty()) {
                size_t bufSize = 0;
                uint8_t* buf = AMediaCodec_getInputBuffer(codec, (size_t)inIdx, &bufSize);
                if (buf && nal.size() <= bufSize) {
                    memcpy(buf, nal.data(), nal.size());
                    AMediaCodec_queueInputBuffer(codec, (size_t)inIdx, 0, nal.size(),
                                                 ptsUs, 0);
                    ptsUs += 33333;  // ~30fps nominal
                } else {
                    LOGW("decoder: input buffer too small (%zu < %zu)",
                         bufSize, nal.size());
                    AMediaCodec_queueInputBuffer(codec, (size_t)inIdx, 0, 0, ptsUs, 0);
                }
            } else {
                // No data: return the buffer unfilled.
                AMediaCodec_queueInputBuffer(codec, (size_t)inIdx, 0, 0, ptsUs, 0);
            }
        }

        // --- drain output ---
        AMediaCodecBufferInfo info;
        ssize_t outIdx = AMediaCodec_dequeueOutputBuffer(codec, &info, kOutputTimeoutUs);
        if (outIdx >= 0) {
            // Render to the Surface (SurfaceTexture) when size > 0.
            AMediaCodec_releaseOutputBuffer(codec, (size_t)outIdx, info.size > 0);
        } else if (outIdx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            LOGI("decoder: output format changed");
        }
    }

    AMediaCodec_stop(codec);
    AMediaCodec_delete(codec);
    LOGI("decoder: stopped");
}

// ---------------- VideoClient ----------------

bool VideoClient::Start(const std::string& host, VideoDecoder* decoder,
                        int* outWidth, int* outHeight) {
    if (running_) return true;
    decoder_ = decoder;

    TcpClient tcp;
    if (!tcp.Connect(host, kVideoPort, 8000)) {
        LOGW("video: connect to %s:%d failed", host.c_str(), kVideoPort);
        return false;
    }
    if (!tcp.SendLine("{\"hello\":\"xr-wrist\",\"v\":1}")) {
        LOGW("video: hello send failed");
        return false;
    }
    std::string resp;
    if (!tcp.RecvLine(resp, 8000)) {
        LOGW("video: handshake response timeout");
        return false;
    }
    LOGI("video: handshake: %s", resp.c_str());

    // Parse {"width":720,"height":1280,"fps":30}
    int w = 720, h = 1280;
    auto num = [&](const char* key) {
        std::string pat = std::string("\"") + key + "\"";
        auto p = resp.find(pat);
        if (p == std::string::npos) return 0;
        p = resp.find(':', p);
        if (p == std::string::npos) return 0;
        return atoi(resp.c_str() + p + 1);
    };
    int pw = num("width"), ph = num("height");
    if (pw > 0 && ph > 0) {
        w = pw;
        h = ph;
    }
    if (outWidth) *outWidth = w;
    if (outHeight) *outHeight = h;

    // Hand the connected socket to the recv thread. We do this by
    // releasing ownership: create the socket here and pass the fd.
    // TcpClient has no release(), so instead reconnect inside the thread
    // is wasteful; simplest: keep this TcpClient alive in a member.
    // For v1, do the handshake on a temp client, then the thread opens
    // its own connection and REPEATS the handshake (idempotent).
    tcp.Close();

    host_ = host;
    running_ = true;
    thread_ = std::thread(&VideoClient::RecvLoop, this);
    return true;
}

void VideoClient::Stop() {
    running_ = false;
    tcp_.ShutdownRecv();
    if (thread_.joinable()) thread_.join();
    tcp_.Close();
    decoder_ = nullptr;
}

void VideoClient::RecvLoop() {
    if (!tcp_.Connect(host_, kVideoPort, 8000)) {
        LOGW("video: recv thread connect failed");
        running_ = false;
        return;
    }
    // Repeat handshake on the streaming connection (server treats it as idempotent).
    tcp_.SendLine("{\"hello\":\"xr-wrist\",\"v\":1}");
    std::string resp;
    tcp_.RecvLine(resp, 8000);

    LOGI("video: streaming started");
    while (running_) {
        uint8_t lenBuf[4];
        if (!tcp_.RecvExact(lenBuf, 4)) break;
        uint32_t nalLen = (uint32_t(lenBuf[0]) << 24) | (uint32_t(lenBuf[1]) << 16) |
                          (uint32_t(lenBuf[2]) << 8) | uint32_t(lenBuf[3]);
        if (nalLen == 0 || nalLen > 8 * 1024 * 1024) {
            LOGW("video: bad NAL length %u", nalLen);
            break;
        }
        std::vector<uint8_t> nal(nalLen);
        if (!tcp_.RecvExact(nal.data(), nalLen)) break;
        if (decoder_) decoder_->FeedNal(nal.data(), nal.size());
    }
    LOGW("video: stream ended");
    running_ = false;
}

}  // namespace xrwrist
