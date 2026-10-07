package com.zachery.xrwrist.phone

import android.accessibilityservice.AccessibilityService
import android.accessibilityservice.GestureDescription
import android.graphics.Path
import android.util.Log
import android.view.accessibility.AccessibilityEvent

/**
 * Injects touch gestures received from the VR headset.
 * User must enable "XR Wrist Touch" in Settings > Accessibility.
 *
 * Touch handling follows scrcpy's philosophy of treating a
 * down->move*->up sequence as one continuous gesture: strokes are
 * chained with willContinue so moves glide instead of stuttering
 * as a series of independent 50ms taps.
 */
class TouchService : AccessibilityService() {

    companion object {
        private const val TAG = "XRWristTouch"
        @Volatile var instance: TouchService? = null
            private set

        // Stroke slice duration. Short slices keep latency low; chaining
        // with willContinue keeps motion smooth.
        private const val SLICE_MS = 16L
    }

    override fun onServiceConnected() {
        super.onServiceConnected()
        instance = this
        Log.i(TAG, "Touch service connected")
    }

    override fun onUnbind(intent: android.content.Intent?): Boolean {
        instance = null
        synchronized(gestureLock) { gestureActive = false }
        return super.onUnbind(intent)
    }

    override fun onAccessibilityEvent(event: AccessibilityEvent?) {}
    override fun onInterrupt() {}

    private val gestureLock = Any()

    /** Last injected point, for chaining move strokes. */
    private var lastX = 0f
    private var lastY = 0f

    /** True while a down->...->up sequence is in progress. */
    private var gestureActive = false

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

        synchronized(gestureLock) {
            when (action) {
                "down" -> {
                    gestureActive = true
                    lastX = x
                    lastY = y
                    dispatchSlice(svc, x, y, x, y, willContinue = true)
                }
                "move" -> {
                    if (!gestureActive) {
                        // Move without a down: treat as a fresh tap-point.
                        gestureActive = true
                        lastX = x
                        lastY = y
                        dispatchSlice(svc, x, y, x, y, willContinue = true)
                    } else {
                        // Chain from the last point for smooth motion.
                        val fromX = lastX
                        val fromY = lastY
                        lastX = x
                        lastY = y
                        dispatchSlice(svc, fromX, fromY, x, y, willContinue = true)
                    }
                }
                "up" -> {
                    if (!gestureActive) return
                    gestureActive = false
                    // Final slice ends the gesture.
                    dispatchSlice(svc, lastX, lastY, x, y, willContinue = false)
                    lastX = x
                    lastY = y
                }
                else -> Log.w(TAG, "unknown touch action: $action")
            }
        }
    }

    /**
     * Dispatch one stroke slice. Chained slices (willContinue=true) form
     * a single continuous gesture, which the system renders as smooth
     * motion instead of discrete taps.
     */
    private fun dispatchSlice(
        svc: AccessibilityService,
        fromX: Float, fromY: Float,
        toX: Float, toY: Float,
        willContinue: Boolean
    ) {
        val path = Path().apply {
            moveTo(fromX, fromY)
            lineTo(toX, toY)
        }
        val stroke = GestureDescription.StrokeDescription(path, 0, SLICE_MS, willContinue)
        val gesture = GestureDescription.Builder().addStroke(stroke).build()

        val dispatched = svc.dispatchGesture(gesture, object : GestureResultCallback() {
            override fun onCompleted(gestureDescription: GestureDescription?) {
                if (DevSettings.verboseLogging) Log.d(TAG, "slice to ($toX,$toY) ok")
            }
            override fun onCancelled(gestureDescription: GestureDescription?) {
                Log.w(TAG, "touch slice cancelled")
                synchronized(gestureLock) { gestureActive = false }
            }
        }, null)

        if (!dispatched) {
            Log.w(TAG, "dispatchGesture returned false")
            gestureActive = false
        }
    }

    /** Simulate power button to turn screen off/on. */
    fun pressPower() {
        val svc = instance ?: return
        svc.performGlobalAction(GLOBAL_ACTION_POWER_DIALOG)
    }
}
