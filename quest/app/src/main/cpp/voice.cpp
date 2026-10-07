// v0.4.0: JNI bridge to Android SpeechRecognizer.
#include "voice.h"

#include "log.h"

namespace xrwrist {

bool VoiceBridge::Init(JavaVM* vm, jobject activity) {
    if (!vm || !activity) return false;
    vm_ = vm;
    JNIEnv* env = nullptr;
    if (vm_->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) return false;

    // FindClass from native code uses the system loader, not the app's.
    // Go through the activity's ClassLoader explicitly.
    jclass activityClass = env->GetObjectClass(activity);
    jmethodID getLoader = env->GetMethodID(
        activityClass, "getClassLoader", "()Ljava/lang/ClassLoader;");
    jobject loader = env->CallObjectMethod(activity, getLoader);
    jclass loaderClass = env->FindClass("java/lang/ClassLoader");
    jmethodID loadClass = env->GetMethodID(
        loaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    jstring name =
        env->NewStringUTF("com.zachery.xrwrist.quest.VoiceHelper");
    jclass cls =
        (jclass)env->CallObjectMethod(loader, loadClass, name);
    env->DeleteLocalRef(name);
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (!cls) {
        LOGW("voice: VoiceHelper class not found via ClassLoader");
        return false;
    }
    jmethodID ctor = env->GetMethodID(cls, "<init>",
                                      "(Landroid/app/Activity;)V");
    if (!ctor) {
        LOGW("voice: VoiceHelper ctor not found");
        return false;
    }
    jobject local = env->NewObject(cls, ctor, activity);
    if (!local || env->ExceptionCheck()) {
        env->ExceptionClear();
        LOGW("voice: VoiceHelper construction failed");
        return false;
    }
    helper_ = env->NewGlobalRef(local);
    env->DeleteLocalRef(local);
    midStart_ = env->GetMethodID(cls, "startListening", "()V");
    midPoll_ = env->GetMethodID(cls, "pollResult", "()Ljava/lang/String;");
    midIsListening_ = env->GetMethodID(cls, "isListening", "()Z");
    if (!midStart_ || !midPoll_ || !midIsListening_) {
        LOGW("voice: VoiceHelper methods missing");
        Shutdown();
        return false;
    }
    ok_ = true;
    LOGI("voice: bridge ready");
    return true;
}

void VoiceBridge::Shutdown() {
    if (!vm_ || !helper_) return;
    JNIEnv* env = nullptr;
    if (vm_->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_OK) {
        env->DeleteGlobalRef(helper_);
    }
    helper_ = nullptr;
    vm_ = nullptr;
    ok_ = false;
}

void VoiceBridge::StartListening() {
    if (!ok_) return;
    JNIEnv* env = nullptr;
    if (vm_->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) return;
    env->CallVoidMethod(helper_, midStart_);
    if (env->ExceptionCheck()) env->ExceptionClear();
}

std::string VoiceBridge::PollResult(bool& listening) {
    listening = false;
    if (!ok_) return "";
    JNIEnv* env = nullptr;
    if (vm_->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) return "";
    listening = env->CallBooleanMethod(helper_, midIsListening_);
    jstring js =
        (jstring)env->CallObjectMethod(helper_, midPoll_);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return "";
    }
    if (!js) return "";
    const char* c = env->GetStringUTFChars(js, nullptr);
    std::string out = c ? c : "";
    if (c) env->ReleaseStringUTFChars(js, c);
    env->DeleteLocalRef(js);
    return out;
}

}  // namespace xrwrist
