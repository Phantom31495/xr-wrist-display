#include "input.h"
#include "log.h"

#include <cstring>
#include <vector>

namespace xrwrist {
namespace {

XrPath StrToPath(XrInstance instance, const char* str) {
    XrPath path = XR_NULL_PATH;
    xrStringToPath(instance, str, &path);
    return path;
}

XrActionSuggestedBinding Binding(XrInstance instance, XrAction action,
                                 const char* pathStr) {
    XrActionSuggestedBinding b{};
    b.action = action;
    b.binding = StrToPath(instance, pathStr);
    return b;
}

XrInput::Pose PoseFromSpace(XrSpace space, XrSpace base, XrTime time) {
    XrInput::Pose p;
    XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
    if (xrLocateSpace(space, base, time, &loc) != XR_SUCCESS) return p;
    if ((loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) &&
        (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) {
        p.valid = true;
        p.pos = {loc.pose.position.x, loc.pose.position.y, loc.pose.position.z};
        p.quat = {loc.pose.orientation.x, loc.pose.orientation.y,
                  loc.pose.orientation.z, loc.pose.orientation.w};
    }
    return p;
}

}  // namespace

bool XrInput::CreateActionSet(XrInstance instance) {
    XrActionSetCreateInfo asInfo{XR_TYPE_ACTION_SET_CREATE_INFO};
    strcpy(asInfo.actionSetName, "wrist_display");
    strcpy(asInfo.localizedActionSetName, "Wrist Display");
    asInfo.priority = 0;
    if (xrCreateActionSet(instance, &asInfo, &actionSet_) != XR_SUCCESS) {
        LOGE("input: xrCreateActionSet failed");
        return false;
    }

    auto makeAction = [&](const char* name, const char* localized,
                          XrActionType type, const char** subpaths,
                          uint32_t subCount, XrAction* out) {
        XrActionCreateInfo ai{XR_TYPE_ACTION_CREATE_INFO};
        ai.actionType = type;
        strcpy(ai.actionName, name);
        strcpy(ai.localizedActionName, localized);
        XrPath subactionArray[2];
        ai.countSubactionPaths = subCount;
        for (uint32_t i = 0; i < subCount && i < 2; ++i)
            subactionArray[i] = StrToPath(instance, subpaths[i]);
        ai.subactionPaths = subactionArray;
        return xrCreateAction(actionSet_, &ai, out) == XR_SUCCESS;
    };

    const char* hands[] = {"/user/hand/left", "/user/hand/right"};
    const char* leftOnly[] = {"/user/hand/left"};
    const char* rightOnly[] = {"/user/hand/right"};
    if (!makeAction("grip_pose", "Grip Pose", XR_ACTION_TYPE_POSE_INPUT, hands, 2,
                    &gripPoseAction_))
        return false;
    if (!makeAction("aim_pose", "Aim Pose", XR_ACTION_TYPE_POSE_INPUT, hands, 2,
                    &aimPoseAction_))
        return false;
    if (!makeAction("trigger", "Trigger", XR_ACTION_TYPE_FLOAT_INPUT, rightOnly, 1,
                    &triggerAction_))
        return false;
    // Avatar articulation (Touch controllers only).
    if (!makeAction("trigger_left", "Left Trigger", XR_ACTION_TYPE_FLOAT_INPUT,
                    leftOnly, 1, &triggerLeftAction_))
        return false;
    if (!makeAction("squeeze", "Left Squeeze", XR_ACTION_TYPE_FLOAT_INPUT,
                    leftOnly, 1, &squeezeAction_))
        return false;
    if (!makeAction("squeeze_right", "Right Squeeze", XR_ACTION_TYPE_FLOAT_INPUT,
                    rightOnly, 1, &squeezeRightAction_))
        return false;
    if (!makeAction("thumbstick", "Left Thumbstick",
                    XR_ACTION_TYPE_VECTOR2F_INPUT, leftOnly, 1,
                    &thumbstickAction_))
        return false;
    if (!makeAction("thumbstick_right", "Right Thumbstick",
                    XR_ACTION_TYPE_VECTOR2F_INPUT, rightOnly, 1,
                    &thumbstickRightAction_))
        return false;
    // Face buttons.
    if (!makeAction("btn_a", "A Button", XR_ACTION_TYPE_BOOLEAN_INPUT,
                    rightOnly, 1, &aAction_))
        return false;
    if (!makeAction("btn_b", "B Button", XR_ACTION_TYPE_BOOLEAN_INPUT,
                    rightOnly, 1, &bAction_))
        return false;
    if (!makeAction("btn_x", "X Button", XR_ACTION_TYPE_BOOLEAN_INPUT, leftOnly, 1,
                    &xAction_))
        return false;
    if (!makeAction("btn_y", "Y Button", XR_ACTION_TYPE_BOOLEAN_INPUT, leftOnly, 1,
                    &yAction_))
        return false;
    // v0.5.0 (SG-4): XR_EXT_hand_interaction — canonical poke/pinch.
    // These give us runtime-tuned fingertip positions and pinch detection.
    if (!makeAction("poke_pose", "Poke Pose", XR_ACTION_TYPE_POSE_INPUT,
                    hands, 2, &pokePoseAction_))
        return false;
    if (!makeAction("pinch_value", "Pinch Value", XR_ACTION_TYPE_FLOAT_INPUT,
                    hands, 2, &pinchAction_))
        return false;

    // Suggested bindings: simple + touch profiles. Analog articulation
    // (trigger/squeeze/thumbstick) only exists on Touch; suggesting those
    // paths to the simple profile would invalidate the whole call.
    const char* profiles[] = {
        "/interaction_profiles/khr/simple_controller",
        "/interaction_profiles/oculus/touch_controller",
    };
    for (const char* profile : profiles) {
        bool isTouch = strstr(profile, "oculus") != nullptr;
        std::vector<XrActionSuggestedBinding> bindings;
        // grip pose
        bindings.push_back(Binding(instance, gripPoseAction_, "/user/hand/left/input/grip/pose"));
        bindings.push_back(Binding(instance, gripPoseAction_, "/user/hand/right/input/grip/pose"));
        // aim pose
        bindings.push_back(Binding(instance, aimPoseAction_, "/user/hand/left/input/aim/pose"));
        bindings.push_back(Binding(instance, aimPoseAction_, "/user/hand/right/input/aim/pose"));
        if (isTouch) {
            // trigger values
            bindings.push_back(Binding(instance, triggerAction_, "/user/hand/right/input/trigger/value"));
            bindings.push_back(Binding(instance, triggerLeftAction_, "/user/hand/left/input/trigger/value"));
            // avatar articulation: squeeze + thumbstick, both hands
            bindings.push_back(Binding(instance, squeezeAction_, "/user/hand/left/input/squeeze/value"));
            bindings.push_back(Binding(instance, squeezeRightAction_, "/user/hand/right/input/squeeze/value"));
            bindings.push_back(Binding(instance, thumbstickAction_, "/user/hand/left/input/thumbstick"));
            bindings.push_back(Binding(instance, thumbstickRightAction_, "/user/hand/right/input/thumbstick"));
            // Face buttons: X/Y left, A/B right.
            bindings.push_back(Binding(instance, xAction_, "/user/hand/left/input/x/click"));
            bindings.push_back(Binding(instance, yAction_, "/user/hand/left/input/y/click"));
            bindings.push_back(Binding(instance, aAction_, "/user/hand/right/input/a/click"));
            bindings.push_back(Binding(instance, bAction_, "/user/hand/right/input/b/click"));
        } else {
            bindings.push_back(Binding(instance, triggerAction_, "/user/hand/right/input/trigger/value"));
            bindings.push_back(Binding(instance, xAction_, "/user/hand/left/input/menu/click"));
            bindings.push_back(Binding(instance, yAction_, "/user/hand/right/input/menu/click"));
        }
        XrInteractionProfileSuggestedBinding suggested{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
        suggested.interactionProfile = StrToPath(instance, profile);
        suggested.suggestedBindings = bindings.data();
        suggested.countSuggestedBindings = (uint32_t)bindings.size();
        XrResult r = xrSuggestInteractionProfileBindings(instance, &suggested);
        if (r != XR_SUCCESS) LOGW("input: suggest bindings failed for %s (%d)", profile, r);
    }

    // v0.5.0 (SG-4): XR_EXT_hand_interaction profile — canonical poke/pinch.
    // This is optional; if the runtime doesn't support it, we fall back to
    // raw joint math. Probe, never assume.
    {
        std::vector<XrActionSuggestedBinding> hiBindings;
        hiBindings.push_back(Binding(instance, pokePoseAction_,
            "/user/hand/left/input/poke_ext/pose"));
        hiBindings.push_back(Binding(instance, pokePoseAction_,
            "/user/hand/right/input/poke_ext/pose"));
        hiBindings.push_back(Binding(instance, pinchAction_,
            "/user/hand/left/input/pinch_ext/value"));
        hiBindings.push_back(Binding(instance, pinchAction_,
            "/user/hand/right/input/pinch_ext/value"));
        // Also bind aim/grip so the hand profile can serve as a complete
        // input source when hands are the active modality.
        hiBindings.push_back(Binding(instance, aimPoseAction_,
            "/user/hand/left/input/aim/pose"));
        hiBindings.push_back(Binding(instance, aimPoseAction_,
            "/user/hand/right/input/aim/pose"));
        hiBindings.push_back(Binding(instance, gripPoseAction_,
            "/user/hand/left/input/grip/pose"));
        hiBindings.push_back(Binding(instance, gripPoseAction_,
            "/user/hand/right/input/grip/pose"));
        XrInteractionProfileSuggestedBinding hiSuggested{
            XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
        hiSuggested.interactionProfile = StrToPath(
            instance, "/interaction_profiles/ext/hand_interaction_ext");
        hiSuggested.suggestedBindings = hiBindings.data();
        hiSuggested.countSuggestedBindings = (uint32_t)hiBindings.size();
        XrResult hr = xrSuggestInteractionProfileBindings(instance, &hiSuggested);
        if (hr == XR_SUCCESS) {
            handInteractionAvailable_ = true;
            LOGI("input: hand interaction profile bound (SG-4)");
        } else {
            LOGW("input: hand interaction profile not available (%d), "
                 "using raw joints", hr);
        }
    }

    // Action spaces.
    auto makeSpace = [&](XrAction action, const char* subpath, XrSpace* out) {
        XrActionSpaceCreateInfo sci{XR_TYPE_ACTION_SPACE_CREATE_INFO};
        sci.action = action;
        sci.subactionPath = StrToPath(instance, subpath);
        sci.poseInActionSpace.orientation.w = 1.0f;
        return xrCreateActionSpace(session_, &sci, out) == XR_SUCCESS;
    };
    if (!makeSpace(gripPoseAction_, "/user/hand/left", &gripSpaceLeft_)) return false;
    if (!makeSpace(gripPoseAction_, "/user/hand/right", &gripSpaceRight_)) return false;
    if (!makeSpace(aimPoseAction_, "/user/hand/right", &aimSpaceRight_)) return false;
    // v0.5.0 (SG-4): poke spaces (optional — only if profile bound).
    if (handInteractionAvailable_) {
        if (!makeSpace(pokePoseAction_, "/user/hand/left", &pokeSpaceLeft_))
            LOGW("input: left poke space failed");
        if (!makeSpace(pokePoseAction_, "/user/hand/right", &pokeSpaceRight_))
            LOGW("input: right poke space failed");
    }

    // Attach to session.
    XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attach.countActionSets = 1;
    attach.actionSets = &actionSet_;
    if (xrAttachSessionActionSets(session_, &attach) != XR_SUCCESS) {
        LOGE("input: attach action sets failed");
        return false;
    }
    LOGI("input: actions ready");
    return true;
}

bool XrInput::Init(XrInstance instance, XrSession session, bool handTrackingEnabled) {
    instance_ = instance;
    session_ = session;
    if (!CreateActionSet(instance)) return false;

    // Hand tracking (optional; only probed when the instance enabled it).
    if (handTrackingEnabled &&
        xrGetInstanceProcAddr(instance, "xrCreateHandTrackerEXT",
                              (PFN_xrVoidFunction*)&xrCreateHandTrackerEXT_) == XR_SUCCESS &&
        xrCreateHandTrackerEXT_) {
        xrGetInstanceProcAddr(instance, "xrDestroyHandTrackerEXT",
                              (PFN_xrVoidFunction*)&xrDestroyHandTrackerEXT_);
        xrGetInstanceProcAddr(instance, "xrLocateHandJointsEXT",
                              (PFN_xrVoidFunction*)&xrLocateHandJointsEXT_);
        XrHandTrackerCreateInfoEXT htInfo{XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT};
        htInfo.hand = XR_HAND_LEFT_EXT;
        htInfo.handJointSet = XR_HAND_JOINT_SET_DEFAULT_EXT;
        if (xrCreateHandTrackerEXT_(session, &htInfo, &handTrackerLeft_) == XR_SUCCESS) {
            handTrackingAvailable_ = true;
            LOGI("input: left hand tracker created");
        } else {
            LOGW("input: hand tracker creation failed");
        }
        // Right hand tracker for direct finger-touch on the display.
        htInfo.hand = XR_HAND_RIGHT_EXT;
        if (xrCreateHandTrackerEXT_(session, &htInfo, &handTrackerRight_) == XR_SUCCESS) {
            LOGI("input: right hand tracker created");
        } else {
            LOGW("input: right hand tracker creation failed");
        }
    } else {
        LOGW("input: XR_EXT_hand_tracking not available");
    }
    memset(wristJoints_, 0, sizeof(wristJoints_));
    memset(rightJoints_, 0, sizeof(rightJoints_));
    return true;
}

void XrInput::Shutdown() {
    if (handTrackerLeft_ != XR_NULL_HANDLE && xrDestroyHandTrackerEXT_) {
        xrDestroyHandTrackerEXT_(handTrackerLeft_);
        handTrackerLeft_ = XR_NULL_HANDLE;
    }
    if (handTrackerRight_ != XR_NULL_HANDLE && xrDestroyHandTrackerEXT_) {
        xrDestroyHandTrackerEXT_(handTrackerRight_);
        handTrackerRight_ = XR_NULL_HANDLE;
    }
    auto destroySpace = [&](XrSpace& s) {
        if (s != XR_NULL_HANDLE) {
            xrDestroySpace(s);
            s = XR_NULL_HANDLE;
        }
    };
    destroySpace(gripSpaceLeft_);
    destroySpace(gripSpaceRight_);
    destroySpace(aimSpaceRight_);
    auto destroyAction = [&](XrAction& a) {
        if (a != XR_NULL_HANDLE) {
            xrDestroyAction(a);
            a = XR_NULL_HANDLE;
        }
    };
    destroyAction(gripPoseAction_);
    destroyAction(aimPoseAction_);
    destroyAction(triggerAction_);
    destroyAction(triggerLeftAction_);
    destroyAction(squeezeAction_);
    destroyAction(squeezeRightAction_);
    destroyAction(thumbstickAction_);
    destroyAction(thumbstickRightAction_);
    destroyAction(xAction_);
    destroyAction(yAction_);
    destroyAction(aAction_);
    destroyAction(bAction_);
    if (actionSet_ != XR_NULL_HANDLE) {
        xrDestroyActionSet(actionSet_);
        actionSet_ = XR_NULL_HANDLE;
    }
}

void XrInput::Poll(XrTime predictedTime, XrSpace appSpace) {
    toggleEdge_ = false;
    aEdge_ = false;
    bEdge_ = false;
    if (session_ == XR_NULL_HANDLE) return;

    // Sync actions.
    XrActiveActionSet active{actionSet_, XR_NULL_PATH};
    XrActionsSyncInfo syncInfo{XR_TYPE_ACTIONS_SYNC_INFO};
    syncInfo.countActiveActionSets = 1;
    syncInfo.activeActionSets = &active;
    if (xrSyncActions(session_, &syncInfo) != XR_SUCCESS) return;

    // Poses.
    aimRight_ = PoseFromSpace(aimSpaceRight_, appSpace, predictedTime);
    gripLeft_ = PoseFromSpace(gripSpaceLeft_, appSpace, predictedTime);
    gripRight_ = PoseFromSpace(gripSpaceRight_, appSpace, predictedTime);
    // Head pose is located by xr_core (VIEW space) and set via SetHead().

    // v0.5.0 (SG-4): canonical poke poses + pinch values from the hand
    // interaction profile. Preferred over raw joint math for intent.
    if (handInteractionAvailable_) {
        pokeLeft_ = PoseFromSpace(pokeSpaceLeft_, appSpace, predictedTime);
        pokeRight_ = PoseFromSpace(pokeSpaceRight_, appSpace, predictedTime);
        auto getPinch = [&](const char* subpath) -> float {
            XrActionStateFloat st{XR_TYPE_ACTION_STATE_FLOAT};
            XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
            gi.action = pinchAction_;
            gi.subactionPath = StrToPath(instance_, subpath);
            if (xrGetActionStateFloat(session_, &gi, &st) == XR_SUCCESS &&
                st.isActive) {
                return st.currentState;
            }
            return 0.0f;
        };
        pinchLeft_ = getPinch("/user/hand/left");
        pinchRight_ = getPinch("/user/hand/right");
    }

    // Trigger float.
    {
        XrActionStateFloat st{XR_TYPE_ACTION_STATE_FLOAT};
        XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
        gi.action = triggerAction_;
        gi.subactionPath = StrToPath(instance_, "/user/hand/right");
        if (xrGetActionStateFloat(session_, &gi, &st) == XR_SUCCESS && st.isActive) {
            trigger_ = st.currentState;
        }
    }

    // Avatar articulation, both hands.
    auto getFloat = [&](XrAction a, const char* subpath) -> float {
        if (a == XR_NULL_HANDLE) return 0.0f;
        XrActionStateFloat st{XR_TYPE_ACTION_STATE_FLOAT};
        XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
        gi.action = a;
        gi.subactionPath = StrToPath(instance_, subpath);
        if (xrGetActionStateFloat(session_, &gi, &st) == XR_SUCCESS &&
            st.isActive) {
            return st.currentState;
        }
        return 0.0f;
    };
    auto getVec2 = [&](XrAction a, const char* subpath, float& x, float& y) {
        if (a == XR_NULL_HANDLE) return;
        XrActionStateVector2f st{XR_TYPE_ACTION_STATE_VECTOR2F};
        XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
        gi.action = a;
        gi.subactionPath = StrToPath(instance_, subpath);
        if (xrGetActionStateVector2f(session_, &gi, &st) == XR_SUCCESS &&
            st.isActive) {
            x = st.currentState.x;
            y = st.currentState.y;
        }
    };
    triggerLeft_ = getFloat(triggerLeftAction_, "/user/hand/left");
    squeezeLeft_ = getFloat(squeezeAction_, "/user/hand/left");
    squeezeRight_ = getFloat(squeezeRightAction_, "/user/hand/right");
    getVec2(thumbstickAction_, "/user/hand/left", stickLeftX_, stickLeftY_);
    getVec2(thumbstickRightAction_, "/user/hand/right", stickRightX_,
            stickRightY_);

    // X/Y combo -> toggle edge.
    auto btnDown = [&](XrAction a) {
        XrActionStateBoolean st{XR_TYPE_ACTION_STATE_BOOLEAN};
        XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
        gi.action = a;
        gi.subactionPath = XR_NULL_PATH;
        if (xrGetActionStateBoolean(session_, &gi, &st) == XR_SUCCESS && st.isActive)
            return st.currentState != XR_FALSE;
        return false;
    };
    bool x = btnDown(xAction_);
    bool y = btnDown(yAction_);
    if (x && y && !(xDown_ && yDown_)) toggleEdge_ = true;
    xDown_ = x;
    yDown_ = y;

    // A/B face buttons (right controller), edge-triggered.
    bool a = btnDown(aAction_);
    bool b = btnDown(bAction_);
    if (a && !aDown_) aEdge_ = true;
    if (b && !bDown_) bEdge_ = true;
    aDown_ = a;
    bDown_ = b;

    // Hand tracking: both hands' joints (wrist + index tip for touch).
    wristValid_ = false;
    wristValidRight_ = false;
    if (handTrackingAvailable_ && xrLocateHandJointsEXT_) {
        XrHandJointsLocateInfoEXT locateInfo{XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT};
        locateInfo.baseSpace = appSpace;
        locateInfo.time = predictedTime;
        auto locate = [&](XrHandTrackerEXT tracker,
                          XrHandJointLocationEXT* out, bool& valid) {
            if (tracker == XR_NULL_HANDLE) return;
            XrHandJointLocationsEXT locations{XR_TYPE_HAND_JOINT_LOCATIONS_EXT};
            locations.jointCount = XR_HAND_JOINT_COUNT_EXT;
            locations.jointLocations = out;
            if (xrLocateHandJointsEXT_(tracker, &locateInfo, &locations) ==
                    XR_SUCCESS &&
                locations.isActive) {
                const auto& w = out[XR_HAND_JOINT_WRIST_EXT];
                if (w.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) {
                    valid = true;
                    lastHandTime_ = predictedTime;
                }
            }
        };
        locate(handTrackerLeft_, wristJoints_, wristValid_);
        locate(handTrackerRight_, rightJoints_, wristValidRight_);
    }
}

XrInput::Pose XrInput::IndexTip(bool right) const {
    Pose p;
    if (right ? !wristValidRight_ : !wristValid_) return p;
    const auto& jt = (right ? rightJoints_ : wristJoints_)[XR_HAND_JOINT_INDEX_TIP_EXT];
    if (!(jt.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)) return p;
    p.valid = true;
    p.pos = {jt.pose.position.x, jt.pose.position.y, jt.pose.position.z};
    p.quat = {jt.pose.orientation.x, jt.pose.orientation.y,
              jt.pose.orientation.z, jt.pose.orientation.w};
    return p;
}

XrInput::Pose XrInput::LeftWrist() const {
    Pose p;
    if (!wristValid_) return p;
    const auto& w = wristJoints_[XR_HAND_JOINT_WRIST_EXT];
    p.valid = true;
    p.pos = {w.pose.position.x, w.pose.position.y, w.pose.position.z};
    p.quat = {w.pose.orientation.x, w.pose.orientation.y,
              w.pose.orientation.z, w.pose.orientation.w};
    return p;
}

bool RayQuadIntersect(const Vec3& rayOrigin, const Vec3& rayDir,
                      const Vec3& quadCenter, const Quat& quadOrient,
                      float quadW, float quadH, float& outU, float& outV) {
    // Quad faces +Z in local space.
    Vec3 n = quadOrient.rotate(Vec3(0, 0, 1));
    Vec3 right = quadOrient.rotate(Vec3(1, 0, 0));
    Vec3 up = quadOrient.rotate(Vec3(0, 1, 0));
    float denom = n.dot(rayDir);
    if (fabsf(denom) < 1e-6f) return false;
    float t = (quadCenter - rayOrigin).dot(n) / denom;
    if (t < 0) return false;
    Vec3 hit = rayOrigin + rayDir * t;
    Vec3 d = hit - quadCenter;
    float u = d.dot(right) / quadW + 0.5f;
    float v = 0.5f - d.dot(up) / quadH;  // v=0 at top
    if (u < 0 || u > 1 || v < 0 || v > 1) return false;
    outU = u;
    outV = v;
    return true;
}

}  // namespace xrwrist
