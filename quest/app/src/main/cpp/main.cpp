// XR Wrist Display — Quest 3 client entry point.
#include <android_native_app_glue.h>

#include "log.h"
#include "xr_core.h"

namespace xrwrist {
namespace {

struct AppState {
    XrApp xrApp;
    bool initialized = false;
    bool exitRequested = false;
};

void HandleCmd(android_app* app, int32_t cmd) {
    auto* state = static_cast<AppState*>(app->userData);
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            if (!state->initialized) {
                LOGI("APP_CMD_INIT_WINDOW: init XR");
                if (state->xrApp.Init(app)) {
                    state->initialized = true;
                } else {
                    LOGE("XrApp::Init failed");
                    state->exitRequested = true;
                }
            }
            break;
        case APP_CMD_TERM_WINDOW:
            LOGI("APP_CMD_TERM_WINDOW");
            break;
        case APP_CMD_GAINED_FOCUS:
            LOGI("gained focus");
            break;
        case APP_CMD_LOST_FOCUS:
            LOGI("lost focus");
            break;
        case APP_CMD_PAUSE:
            LOGI("paused");
            break;
        case APP_CMD_RESUME:
            LOGI("resumed");
            break;
        case APP_CMD_DESTROY:
            LOGI("destroy");
            state->exitRequested = true;
            break;
        default:
            break;
    }
}

}  // namespace
}  // namespace xrwrist

void android_main(android_app* app) {
    using namespace xrwrist;
    LOGI("XR Wrist Display starting");

    AppState state;
    app->userData = &state;
    app->onAppCmd = HandleCmd;

    // Main loop: pump Android events, then run XR frames.
    while (!state.exitRequested) {
        int events = 0;
        android_poll_source* source = nullptr;
        // Block briefly when not initialized; poll otherwise.
        int timeoutMs = state.initialized ? 0 : 50;
        while (ALooper_pollOnce(timeoutMs, nullptr, &events,
                                reinterpret_cast<void**>(&source)) >= 0) {
            if (source) source->process(app, source);
            if (state.exitRequested) break;
            timeoutMs = 0;  // drain remaining events without blocking
        }
        if (state.exitRequested) break;
        if (!state.initialized) continue;

        state.xrApp.PollEvents(state.exitRequested);
        state.xrApp.Frame();
    }

    LOGI("XR Wrist Display exiting");
    state.xrApp.Shutdown();
}
