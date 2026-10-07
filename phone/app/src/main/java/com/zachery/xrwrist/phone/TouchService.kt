package com.zachery.xrwrist.phone

import android.accessibilityservice.AccessibilityService
import android.accessibilityservice.GestureDescription
import android.graphics.Path
import android.util.Log
import android.view.accessibility.AccessibilityEvent

/**
 * Injects touch gestures received from the VR headset.
 * User must enable "XR Wrist Touch" in Settings > Accessibility.
 */
class TouchService : AccessibilityService() {

    companion object {
        private const val TAG = "XRWristTouch"
        @Volatile var instance: TouchService? = null
            private set
    }

    override fun onServiceConnected() {
        super.onServiceConnected()
        instance = this
        Log.i(TAG, "Touch service connected")
    }

    override fun onUnbind(intent: android.content.Intent?): Boolean {
        instance = null
        return super.onUnbind(intent)
    }

    override fun onAccessibilityEvent(event: AccessibilityEvent?) {}
    override fun onInterrupt() {}

    /**
     * Dispatch a touch at normalized coordinates (0.0-1.0).
     * action: "down", "move", or "up"
     */
    fun touch(action: String, nx: Float, ny: Float) {
        val svc = instance ?: run {
            Log.w(TAG, "Touch service not enabled")
            return
        }
        val dm = svc.resources.displayMetrics
        val x = (nx.coerceIn(0f, 1f) * dm.widthPixels)
        val y = (ny.coerceIn(0f, 1f) * dm.heightPixels)

        val path = Path().apply { moveTo(x, y) }
        val stroke = GestureDescription.StrokeDescription(path, 0, 50)
        val gesture = GestureDescription.Builder().addStroke(stroke).build()

        val dispatched = svc.dispatchGesture(gesture, object : GestureResultCallback() {
            override fun onCompleted(gestureDescription: GestureDescription?) {
                Log.d(TAG, "touch $action at ($x,$y) ok")
            }
            override fun onCancelled(gestureDescription: GestureDescription?) {
                Log.w(TAG, "touch $action cancelled")
            }
        }, null)

        if (!dispatched) Log.w(TAG, "dispatchGesture returned false for $action")
    }

    /** Simulate power button to turn screen off/on. */
    fun pressPower() {
        val svc = instance ?: return
        svc.performGlobalAction(GLOBAL_ACTION_POWER_DIALOG)
    }
}
