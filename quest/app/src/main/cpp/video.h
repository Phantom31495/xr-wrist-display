#pragma once
#include <jni.h>

#include "net.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

struct ANativeWindow;

namespace xrwrist {

// JNI wrapper around android.graphics.SurfaceTexture attached to an
// already-created GL_TEXTURE_EXTERNAL_OES texture. All methods must be
// called on the thread with the EGL context current (the render thread).
class SurfaceTextureHelper {
public:
    SurfaceTextureHelper() = default;
    ~SurfaceTextureHelper() { Release(); }

    // Creates SurfaceTexture(texId) and returns an ANativeWindow for the decoder.
    // Returns nullptr on failure. Must be called on the GL thread.
    ANativeWindow* Create(JavaVM* vm, uint32_t glTexId);
    void Update();  // updateTexImage()
    void GetTransformMatrix(float out16[16]);
    void Release();
    // v0.6.2: number of successful frame updates (for stream health).
    uint64_t FrameCount() const { return frames_; }

private:
    JavaVM* vm_ = nullptr;
    jobject surfaceTextureObj_ = nullptr;  // global ref
    jmethodID updateMid_ = nullptr;
    jmethodID getMatrixMid_ = nullptr;
    ANativeWindow* window_ = nullptr;

    JNIEnv* Env();

    // v0.6.2: incremented each time updateTexImage() delivers a new frame.
    // Called on the GL thread only.
    uint64_t frames_ = 0;
};

// H.264 decoder: feeds Annex-B NAL units into AMediaCodec, renders to the
// ANativeWindow from SurfaceTextureHelper. Runs its own thread.
class VideoDecoder {
public:
    VideoDecoder() = default;
    ~VideoDecoder() { Stop(); }

    bool Start(ANativeWindow* window, int width, int height);
    void Stop();
    // Thread-safe; drops oldest data if the queue grows too large.
    void FeedNal(const uint8_t* data, size_t len);
    bool IsRunning() const { return running_; }

private:
    void DecodeLoop(int width, int height);

    std::atomic<bool> running_{false};
    std::thread thread_;
    ANativeWindow* window_ = nullptr;

    std::mutex mutex_;
    std::queue<std::vector<uint8_t>> queue_;
    static constexpr size_t kMaxQueueBytes = 4 * 1024 * 1024;
    size_t queueBytes_ = 0;
};

// Connects to phone:8900, performs the hello handshake, and streams
// length-prefixed H.264 NAL units into the decoder.
class VideoClient {
public:
    VideoClient() = default;
    ~VideoClient() { Stop(); }

    // Blocks until handshake completes or fails. Returns video params.
    bool Start(const std::string& host, VideoDecoder* decoder,
               int* outWidth, int* outHeight);
    void Stop();
    bool IsRunning() const { return running_; }

private:
    void RecvLoop();

    std::atomic<bool> running_{false};
    std::thread thread_;
    VideoDecoder* decoder_ = nullptr;
    std::string host_;
    TcpClient tcp_;  // owned by RecvLoop thread; Stop() interrupts via ShutdownRecv
};

}  // namespace xrwrist
