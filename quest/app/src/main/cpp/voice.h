#pragma once
// v0.4.0: voice control bridge. Wraps Android's SpeechRecognizer via JNI.
// Push-to-talk: the native side calls StartListening() (e.g. on Y-hold);
// recognition results are polled with PollResult(). All JNI is done on the
// calling thread — call from the render thread only.

#include <jni.h>

#include <string>

namespace xrwrist {

class VoiceBridge {
public:
    VoiceBridge() = default;
    ~VoiceBridge() { Shutdown(); }

    // vm: JavaVM from android_app->activity->vm; activity: NativeActivity obj.
    bool Init(JavaVM* vm, jobject activity);
    void Shutdown();

    // Begin one recognition pass. Safe to call only when not listening.
    void StartListening();
    // Returns "" when nothing new; otherwise the recognized text (caller
    // should ExecuteVoiceCommand on it). Also reports listening state.
    std::string PollResult(bool& listening);

private:
    JavaVM* vm_ = nullptr;
    jobject helper_ = nullptr;  // global ref to VoiceHelper
    jmethodID midStart_ = nullptr;
    jmethodID midPoll_ = nullptr;
    jmethodID midIsListening_ = nullptr;
    bool ok_ = false;
};

}  // namespace xrwrist
