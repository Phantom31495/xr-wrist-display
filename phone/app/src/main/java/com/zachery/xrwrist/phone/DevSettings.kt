package com.zachery.xrwrist.phone

import android.content.Context
import android.content.SharedPreferences

/**
 * Persistent developer settings. All values survive process death and
 * are read by [StreamService] on every (re)start, so changes apply to
 * the next streaming session without an app update.
 */
object DevSettings {

    private const val PREFS = "xrwrist_dev"
    private const val K_VERBOSE = "verbose_logging"
    private const val K_IP_OVERRIDE = "ip_override"
    private const val K_WIDTH = "video_width"
    private const val K_HEIGHT = "video_height"
    private const val K_FPS = "video_fps"
    private const val K_BITRATE = "video_bitrate"
    private const val K_DISCOVERY_REPLY = "discovery_reply"
    private const val K_GODMODE = "godmode_unlocked"

    // Defaults match the original hardcoded pipeline.
    const val DEF_WIDTH = 720
    const val DEF_HEIGHT = 1280
    const val DEF_FPS = 30
    const val DEF_BITRATE = 2_000_000

    @Volatile private var prefs: SharedPreferences? = null

    fun init(ctx: Context) {
        if (prefs == null) {
            prefs = ctx.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        }
    }

    private fun p(): SharedPreferences =
        prefs ?: throw IllegalStateException("DevSettings.init() not called")

    var verboseLogging: Boolean
        get() = p().getBoolean(K_VERBOSE, false)
        set(v) = p().edit().putBoolean(K_VERBOSE, v).apply()

    /** Manual IP override for the discovery reply. Blank = auto-detect. */
    var ipOverride: String
        get() = p().getString(K_IP_OVERRIDE, "") ?: ""
        set(v) = p().edit().putString(K_IP_OVERRIDE, v.trim()).apply()

    var videoWidth: Int
        get() = p().getInt(K_WIDTH, DEF_WIDTH).coerceIn(320, 1920)
        set(v) = p().edit().putInt(K_WIDTH, v.coerceIn(320, 1920)).apply()

    var videoHeight: Int
        get() = p().getInt(K_HEIGHT, DEF_HEIGHT).coerceIn(320, 1920)
        set(v) = p().edit().putInt(K_HEIGHT, v.coerceIn(320, 1920)).apply()

    var videoFps: Int
        get() = p().getInt(K_FPS, DEF_FPS).coerceIn(15, 60)
        set(v) = p().edit().putInt(K_FPS, v.coerceIn(15, 60)).apply()

    var videoBitrate: Int
        get() = p().getInt(K_BITRATE, DEF_BITRATE).coerceIn(500_000, 20_000_000)
        set(v) = p().edit().putInt(K_BITRATE, v.coerceIn(500_000, 20_000_000)).apply()

    /** Custom discovery reply name. Defaults to "ziggy". */
    var discoveryName: String
        get() = p().getString(K_DISCOVERY_REPLY, "ziggy") ?: "ziggy"
        set(v) = p().edit().putString(K_DISCOVERY_REPLY, v.ifBlank { "ziggy" }).apply()

    /** Hidden Godmode section unlocked via triple-tap or the console. */
    var godmodeUnlocked: Boolean
        get() = p().getBoolean(K_GODMODE, false)
        set(v) = p().edit().putBoolean(K_GODMODE, v).apply()

    fun resetToDefaults() {
        p().edit()
            .putBoolean(K_VERBOSE, false)
            .putString(K_IP_OVERRIDE, "")
            .putInt(K_WIDTH, DEF_WIDTH)
            .putInt(K_HEIGHT, DEF_HEIGHT)
            .putInt(K_FPS, DEF_FPS)
            .putInt(K_BITRATE, DEF_BITRATE)
            .putString(K_DISCOVERY_REPLY, "ziggy")
            .apply()
    }

    fun summary(): String = buildString {
        appendLine("verbose=$verboseLogging")
        appendLine("ip_override=${ipOverride.ifBlank { "(auto)" }}")
        appendLine("video=${videoWidth}x${videoHeight}@${videoFps}fps")
        appendLine("bitrate=${videoBitrate / 1_000_000.0} Mbps")
        appendLine("discovery_name=$discoveryName")
    }
}
