package com.zachery.xrwrist.phone

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import android.net.Uri
import android.os.Build
import android.os.PowerManager
import android.provider.Settings
import android.util.DisplayMetrics
import android.util.Log

/**
 * Compatibility layer: smooths over Android version (29 → current) and
 * manufacturer (Samsung / Pixel / Xiaomi / Oppo / …) differences.
 *
 * NOTE: androidx imports are compile-guarded — the manual build has no
 * androidx on the classpath, so every androidx call below goes through
 * [Ax], a tiny reflection shim that degrades to platform APIs.
 */
object Compat {

    private const val TAG = "XRWristCompat"

    val manufacturer: String get() = Build.MANUFACTURER ?: "unknown"
    val model: String get() = Build.MODEL ?: "unknown"
    val sdkInt: Int get() = Build.VERSION.SDK_INT
    val release: String get() = Build.VERSION.RELEASE ?: "?"

    fun isSamsung(): Boolean = manufacturer.equals("samsung", ignoreCase = true)
    fun isXiaomi(): Boolean = manufacturer.equals("xiaomi", ignoreCase = true) ||
        manufacturer.equals("redmi", ignoreCase = true)
    fun isOppo(): Boolean = manufacturer.equals("oppo", ignoreCase = true) ||
        manufacturer.equals("oneplus", ignoreCase = true) ||
        manufacturer.equals("realme", ignoreCase = true)

    fun deviceLine(): String =
        "$manufacturer $model — Android $release (API $sdkInt)"

    // ---------- Permissions ----------

    /** POST_NOTIFICATIONS is runtime-gated from API 33; request it before streaming. */
    fun ensureNotificationPermission(activity: Activity, reqCode: Int = 2001) {
        if (sdkInt < 33) return
        if (!hasPermission(activity, android.Manifest.permission.POST_NOTIFICATIONS)) {
            activity.requestPermissions(
                arrayOf(android.Manifest.permission.POST_NOTIFICATIONS), reqCode)
        }
    }

    fun hasPermission(ctx: Context, perm: String): Boolean =
        ctx.checkSelfPermission(perm) == PackageManager.PERMISSION_GRANTED

    // ---------- Battery / background execution ----------

    /** Samsung & Xiaomi kill background services aggressively; offer the whitelist screen. */
    fun isIgnoringBatteryOptimizations(ctx: Context): Boolean {
        val pm = ctx.getSystemService(Context.POWER_SERVICE) as PowerManager
        return pm.isIgnoringBatteryOptimizations(ctx.packageName)
    }

    fun requestIgnoreBatteryOptimizations(ctx: Context) {
        try {
            val i = Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS).apply {
                data = Uri.parse("package:${ctx.packageName}")
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            }
            ctx.startActivity(i)
        } catch (e: Exception) {
            Log.w(TAG, "battery opt intent failed", e)
            try {
                ctx.startActivity(Intent(Settings.ACTION_IGNORE_BATTERY_OPTIMIZATION_SETTINGS)
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
            } catch (_: Exception) {}
        }
    }

    // ---------- Display metrics ----------

    /** Some OEM ROMs report 0 density on secondary displays; fall back sanely. */
    fun safeDensityDpi(metrics: DisplayMetrics): Int =
        if (metrics.densityDpi > 0) metrics.densityDpi else DisplayMetrics.DENSITY_DEFAULT

    // ---------- Encoder probing ----------

    data class EncoderProbe(
        val ok: Boolean,
        val codecName: String,
        val detail: String
    )

    /**
     * Best-effort probe: can this device actually configure an AVC encoder
     * with the requested parameters? Runs off the main thread.
     */
    fun probeVideoEncoder(width: Int, height: Int, fps: Int, bitrate: Int): EncoderProbe {
        return try {
            val codec = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_VIDEO_AVC)
            val name = try { codec.name } catch (_: Exception) { "unknown" }
            val format = MediaFormat.createVideoFormat(
                MediaFormat.MIMETYPE_VIDEO_AVC, width, height).apply {
                setInteger(MediaFormat.KEY_COLOR_FORMAT,
                    MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface)
                setInteger(MediaFormat.KEY_BIT_RATE, bitrate)
                setInteger(MediaFormat.KEY_FRAME_RATE, fps)
                setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 2)
            }
            codec.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
            val surface = codec.createInputSurface()
            codec.start()
            codec.stop()
            surface.release()
            codec.release()
            EncoderProbe(true, name, "${width}x${height}@${fps}")
        } catch (e: Exception) {
            Log.w(TAG, "encoder probe failed", e)
            EncoderProbe(false, "none", e.message ?: e.toString())
        }
    }

    // ---------- Full diagnostics report ----------

    /**
     * Runs every compatibility check and returns a human-readable report.
     * Call off the main thread (encoder probe blocks briefly).
     */
    fun runDiagnostics(ctx: Context): String {
        val sb = StringBuilder()
        sb.appendLine("── DEVICE ──")
        sb.appendLine(deviceLine())
        sb.appendLine("ABI: ${Build.SUPPORTED_ABIS.firstOrNull() ?: "?"}")
        sb.appendLine()
        sb.appendLine("── OS CAPABILITIES ──")
        sb.appendLine("foreground_service_type (29+): ${sdkInt >= 29}")
        sb.appendLine("runtime notifications (33+): ${sdkInt >= 33}")
        sb.appendLine("parcelable-extra API (33+): ${sdkInt >= 33}")
        sb.appendLine("POST_NOTIFICATIONS granted: " +
            hasPermission(ctx, android.Manifest.permission.POST_NOTIFICATIONS))
        sb.appendLine("ignoring battery optimizations: ${isIgnoringBatteryOptimizations(ctx)}")
        sb.appendLine()
        sb.appendLine("── OEM NOTES ──")
        when {
            isSamsung() -> sb.appendLine(
                "Samsung: APK uses STORED manifest (parse-safe). " +
                "If streams die in background, disable 'Put unused apps to sleep' " +
                "for XR Wrist in Device care → Battery.")
            isXiaomi() -> sb.appendLine(
                "Xiaomi/MIUI: enable 'Autostart' for XR Wrist and set Battery saver " +
                "to 'No restrictions', or the encoder service may be killed.")
            isOppo() -> sb.appendLine(
                "Oppo/OnePlus/Realme: allow 'Background activity' / disable " +
                "'Background freeze' for XR Wrist in App management.")
            else -> sb.appendLine("No OEM-specific workarounds needed.")
        }
        sb.appendLine()
        sb.appendLine("── ENCODER PROBE ──")
        val probe = probeVideoEncoder(
            DevSettings.videoWidth, DevSettings.videoHeight,
            DevSettings.videoFps, DevSettings.videoBitrate)
        sb.appendLine("result: ${if (probe.ok) "OK" else "FAILED"}")
        sb.appendLine("codec: ${probe.codecName}")
        sb.appendLine("detail: ${probe.detail}")
        sb.appendLine()
        sb.appendLine("── WIRE PROTOCOL ──")
        sb.appendLine("discovery UDP ${NetworkServer.PORT_DISCOVERY}")
        sb.appendLine("video TCP ${NetworkServer.PORT_VIDEO} (H.264 Annex B)")
        sb.appendLine("control TCP ${NetworkServer.PORT_CONTROL} (JSON)")
        return sb.toString().trimEnd()
    }
}

/**
 * Reflection shims for androidx (unavailable in the manual build).
 * They resolve to platform APIs so the code compiles with or without
 * the Jetpack classpath.
 */
object Ax {
    fun checkSelfPermission(ctx: Context, perm: String): Int =
        ctx.checkSelfPermission(perm)

    fun requestPermissions(activity: Activity, perms: Array<String>, code: Int) {
        activity.requestPermissions(perms, code)
    }
}
