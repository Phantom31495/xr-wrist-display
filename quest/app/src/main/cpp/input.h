#pragma once
#include <openxr/openxr.h>

#include <string>
#include "gl_render.h"

namespace xrwrist {

// Action-based input + hand tracking for the wrist display.
class XrInput {
public:
    XrInput() = default;
    ~XrInput() { Shutdown(); }

    bool Init(XrInstance instance, XrSession session, bool handTrackingEnabled);
    void Shutdown();

    // Polls actions and hand tracking for the given predicted time.
    // appSpace: the reference space poses are located in.
    void Poll(XrTime predictedTime, XrSpace appSpace);

    struct Pose {
        Vec3 pos;
        Quat quat;
        bool valid = false;
    };

    const Pose& RightAim() const { return aimRight_; }   // controller ray
    const Pose& LeftGrip() const { return gripLeft_; }   // left hand anchor
    const Pose& RightGrip() const { return gripRight_; } // right hand anchor
    const Pose& Head() const { return head_; }
    void SetHead(const Pose& p) { head_ = p; }

    float TriggerValue() const { return trigger_; }      // right, 0..1 (touch)
    bool TogglePressed() const { return toggleEdge_; }   // X+Y combo this frame

    // Left-hand avatar articulation (0..1 unless noted).
    float LeftTrigger() const { return triggerLeft_; }   // index curl
    float LeftSqueeze() const { return squeezeLeft_; }   // full-hand curl
    void LeftThumbstick(float& x, float& y) const {       // -1..1 each
        x = stickLeftX_;
        y = stickLeftY_;
    }
    // Right-hand avatar articulation (0..1 unless noted).
    float RightTrigger() const { return trigger_; }      // index curl
    float RightSqueeze() const { return squeezeRight_; }  // full-hand curl
    void RightThumbstick(float& x, float& y) const {      // -1..1 each
        x = stickRightX_;
        y = stickRightY_;
    }
    // A/B face buttons (right controller), edge-triggered per frame.
    bool APressed() const { return aEdge_; }
    bool BPressed() const { return bEdge_; }
    // Held states (for the input debugger).
    bool XDown() const { return xDown_; }
    bool YDown() const { return yDown_; }
    bool ADown() const { return aDown_; }
    bool BDown() const { return bDown_; }

    // Left wrist from hand tracking (valid only when hands are tracked).
    Pose LeftWrist() const;
    // Index fingertip from hand tracking, in app space. hand: false=left.
    Pose IndexTip(bool right) const;
    bool HandTracked(bool right) const {
        return right ? wristValidRight_ : wristValid_;
    }

    // v0.5.0 (SG-4): XR_EXT_hand_interaction — canonical poke/pinch.
    // PokePose returns the runtime-tuned fingertip poke position.
    // PinchValue returns 0..1 pinch strength (threshold ~0.8 = click).
    // These are preferred over raw joint math for intent detection.
    Pose PokePose(bool right) const {
        return right ? pokeRight_ : pokeLeft_;
    }
    float PinchValue(bool right) const {
        return right ? pinchRight_ : pinchLeft_;
    }
    bool HandInteractionAvailable() const { return handInteractionAvailable_; }

private:
    bool CreateActionSet(XrInstance instance);

    XrInstance instance_ = XR_NULL_HANDLE;
    XrSession session_ = XR_NULL_HANDLE;
    XrActionSet actionSet_ = XR_NULL_HANDLE;
    XrAction gripPoseAction_ = XR_NULL_HANDLE;
    XrAction aimPoseAction_ = XR_NULL_HANDLE;
    XrAction triggerAction_ = XR_NULL_HANDLE;        // right trigger
    XrAction triggerLeftAction_ = XR_NULL_HANDLE;    // left trigger
    XrAction squeezeAction_ = XR_NULL_HANDLE;        // left squeeze
    XrAction squeezeRightAction_ = XR_NULL_HANDLE;   // right squeeze
    XrAction thumbstickAction_ = XR_NULL_HANDLE;     // left thumbstick
    XrAction thumbstickRightAction_ = XR_NULL_HANDLE;// right thumbstick
    XrAction xAction_ = XR_NULL_HANDLE;
    XrAction yAction_ = XR_NULL_HANDLE;
    XrAction aAction_ = XR_NULL_HANDLE;
    XrAction bAction_ = XR_NULL_HANDLE;
    XrSpace gripSpaceLeft_ = XR_NULL_HANDLE;
    XrSpace gripSpaceRight_ = XR_NULL_HANDLE;
    XrSpace aimSpaceRight_ = XR_NULL_HANDLE;

    // v0.5.0 (SG-4): XR_EXT_hand_interaction actions.
    XrAction pokePoseAction_ = XR_NULL_HANDLE;
    XrAction pinchAction_ = XR_NULL_HANDLE;
    XrSpace pokeSpaceLeft_ = XR_NULL_HANDLE;
    XrSpace pokeSpaceRight_ = XR_NULL_HANDLE;
    bool handInteractionAvailable_ = false;
    Pose pokeLeft_;
    Pose pokeRight_;
    float pinchLeft_ = 0.0f;
    float pinchRight_ = 0.0f;

    // Hand tracking (optional).
    PFN_xrCreateHandTrackerEXT xrCreateHandTrackerEXT_ = nullptr;
    PFN_xrDestroyHandTrackerEXT xrDestroyHandTrackerEXT_ = nullptr;
    PFN_xrLocateHandJointsEXT xrLocateHandJointsEXT_ = nullptr;
    XrHandTrackerEXT handTrackerLeft_ = XR_NULL_HANDLE;
    XrHandTrackerEXT handTrackerRight_ = XR_NULL_HANDLE;
    bool handTrackingAvailable_ = false;
    XrHandJointLocationEXT wristJoints_[XR_HAND_JOINT_COUNT_EXT];
    XrHandJointLocationEXT rightJoints_[XR_HAND_JOINT_COUNT_EXT];
    bool wristValid_ = false;
    bool wristValidRight_ = false;
    XrTime lastHandTime_ = 0;

    Pose aimRight_;
    Pose gripLeft_;
    Pose gripRight_;
    Pose head_;
    float trigger_ = 0.0f;
    float triggerLeft_ = 0.0f;
    float squeezeLeft_ = 0.0f;
    float squeezeRight_ = 0.0f;
    float stickLeftX_ = 0.0f;
    float stickLeftY_ = 0.0f;
    float stickRightX_ = 0.0f;
    float stickRightY_ = 0.0f;
    bool toggleEdge_ = false;
    bool aEdge_ = false;
    bool bEdge_ = false;
    bool xDown_ = false;
    bool yDown_ = false;
    bool aDown_ = false;
    bool bDown_ = false;
};

// Ray vs oriented quad. Returns true on hit with uv in [0,1].
bool RayQuadIntersect(const Vec3& rayOrigin, const Vec3& rayDir,
                      const Vec3& quadCenter, const Quat& quadOrient,
                      float quadW, float quadH, float& outU, float& outV);

}  // namespace xrwrist
