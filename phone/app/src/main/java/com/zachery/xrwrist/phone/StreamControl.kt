package com.zachery.xrwrist.phone

/**
 * Control hooks into the live streaming pipeline, exposed to
 * Developer Options and the console. [StreamService] registers the
 * encoder callback on start and clears it on stop.
 */
object StreamControl {
    @Volatile var requestKeyFrame: (() -> Unit)? = null

    fun keyFrame(): Boolean {
        val fn = requestKeyFrame ?: return false
        return try {
            fn(); true
        } catch (_: Exception) { false }
    }
}
