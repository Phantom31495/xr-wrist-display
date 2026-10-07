package com.zachery.xrwrist.phone

import android.content.Intent

/**
 * Holds the last MediaProjection permission grant so the streaming
 * service can be restarted (e.g. from Developer Options or the console)
 * without making the user re-approve the system capture dialog.
 *
 * The token remains valid for the lifetime of the process.
 */
object ProjectionHolder {
    @Volatile var resultCode: Int = 0
    @Volatile var data: Intent? = null

    val hasGrant: Boolean get() = data != null

    fun store(code: Int, intent: Intent) {
        resultCode = code
        // Clone: the Activity's result intent may be recycled.
        data = Intent(intent)
    }

    fun clear() {
        resultCode = 0
        data = null
    }
}
