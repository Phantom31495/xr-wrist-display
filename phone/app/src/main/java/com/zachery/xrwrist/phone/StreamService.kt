package com.zachery.xrwrist.phone

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.hardware.display.DisplayManager
import android.media.projection.MediaProjection
import android.media.projection.MediaProjectionManager
import android.os.Build
import android.os.IBinder
import android.util.DisplayMetrics
import android.util.Log
import android.view.WindowManager

/**
 * Foreground service: captures the screen via MediaProjection,
 * encodes H.264, and streams to the Quest over the wire protocol.
 */
class StreamService : Service() {

    companion object {
        const val EXTRA_RESULT_CODE = "result_code"
        const val EXTRA_DATA = "data"
        const val ACTION_RESTART = "com.zachery.xrwrist.phone.RESTART_STREAM"
        private const val TAG = "XRWristStream"
        private const val NOTIF_ID = 42
        private const val CHANNEL_ID = "xr_wrist_stream"

        @Volatile var isRunning = false
            private set
    }

    private var projection: MediaProjection? = null
    private var encoder: VideoEncoder? = null
    private var net: NetworkServer? = null

    override fun onBind(intent: Intent?): IBinder? = null

    private fun createChannel() {
        val mgr = getSystemService(NotificationManager::class.java)
        val channel = NotificationChannel(
            CHANNEL_ID, "XR Wrist Stream", NotificationManager.IMPORTANCE_LOW)
        mgr.createNotificationChannel(channel)
    }

    override fun onCreate() {
        super.onCreate()
        DevSettings.init(this)
        createChannel()
        val notif = Notification.Builder(this, CHANNEL_ID)
            .setContentTitle("XR Wrist Display")
            .setContentText("Streaming phone screen to Quest")
            .setSmallIcon(android.R.drawable.ic_menu_camera)
            .build()
        if (Build.VERSION.SDK_INT >= 29) {
            startForeground(NOTIF_ID, notif,
                ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION)
        } else {
            startForeground(NOTIF_ID, notif)
        }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        // Admin restart: reuse the stored MediaProjection grant.
        if (intent?.action == ACTION_RESTART) {
            Log.i(TAG, "restart requested")
            if (!ProjectionHolder.hasGrant) {
                Log.e(TAG, "restart failed: no stored projection grant")
                StreamStats.lastError = "restart: no projection grant; re-request permission"
                StreamStats.state = "ERROR"
                stopSelf()
                return START_NOT_STICKY
            }
            tearDown()
            startStreaming(ProjectionHolder.resultCode, ProjectionHolder.data!!)
            return START_STICKY
        }
        if (isRunning) return START_STICKY
        val resultCode = intent?.getIntExtra(EXTRA_RESULT_CODE, 0) ?: 0
        val data: Intent? = if (Build.VERSION.SDK_INT >= 33) {
            intent?.getParcelableExtra(EXTRA_DATA, Intent::class.java)
        } else {
            @Suppress("DEPRECATION")
            intent?.getParcelableExtra(EXTRA_DATA)
        }
        if (data == null) {
            Log.e(TAG, "no projection data; stopping")
            StreamStats.lastError = "no projection data"
            StreamStats.state = "ERROR"
            stopSelf()
            return START_NOT_STICKY
        }
        ProjectionHolder.store(resultCode, data)
        startStreaming(resultCode, data)
        return START_STICKY
    }

    private fun startStreaming(resultCode: Int, data: Intent) {
        StreamStats.reset()
        StreamStats.state = "STARTING"
        // Video pipeline parameters come from Developer Options (persisted).
        val vidW = DevSettings.videoWidth
        val vidH = DevSettings.videoHeight
        val vidFps = DevSettings.videoFps
        val vidBitrate = DevSettings.videoBitrate
        try {
            val mpm = getSystemService(Context.MEDIA_PROJECTION_SERVICE)
                as MediaProjectionManager
            projection = mpm.getMediaProjection(resultCode, data)
            StreamStats.stateDetail = "projection acquired"

            // Network first so clients can connect while encoder spins up
            val server = NetworkServer(
                videoWidth = vidW, videoHeight = vidH, videoFps = vidFps,
                onTouch = { action, x, y ->
                    TouchService.instance?.touch(action, x, y)
                        ?: Log.w(TAG, "touch ignored: service not enabled")
                },
                onCommand = { cmd -> handleCommand(cmd) }
            )
            server.start()
            net = server
            StreamStats.stateDetail = "network up"

            val enc = VideoEncoder(vidW, vidH, vidFps, vidBitrate) { nal ->
                server.sendVideoNal(nal)
            }
            enc.start()
            encoder = enc
            StreamControl.requestKeyFrame = { enc.requestKeyFrame() }
            StreamStats.stateDetail = "encoder up"

            // Feed encoder surface from a virtual display
            val wm = getSystemService(Context.WINDOW_SERVICE) as WindowManager
            val metrics = DisplayMetrics()
            @Suppress("DEPRECATION")
            wm.defaultDisplay.getRealMetrics(metrics)
            // Some OEM ROMs report 0 density on virtual displays — fall back sanely.
            val density = Compat.safeDensityDpi(metrics)

            val surface = enc.inputSurface
                ?: throw IllegalStateException("no encoder surface")
            projection?.registerCallback(object : MediaProjection.Callback() {
                override fun onStop() { stopSelf() }
            }, android.os.Handler(android.os.Looper.getMainLooper()))
            projection?.createVirtualDisplay(
                "XRWristDisplay",
                vidW, vidH, density,
                DisplayManager.VIRTUAL_DISPLAY_FLAG_AUTO_MIRROR,
                surface, null, null
            )

            isRunning = true
            StreamStats.state = "STREAMING"
            StreamStats.stateDetail = "${vidW}x${vidH}@${vidFps}"
            StreamStats.streamStartTimeMs = System.currentTimeMillis()
            Log.i(TAG, "streaming started ${vidW}x${vidH}@${vidFps} ${vidBitrate}bps")
        } catch (e: Exception) {
            Log.e(TAG, "startStreaming failed", e)
            StreamStats.lastError = e.message ?: e.toString()
            StreamStats.state = "ERROR"
            stopSelf()
        }
    }

    private fun handleCommand(cmd: String) {
        Log.i(TAG, "command: $cmd")
        when (cmd) {
            "screen_off", "screen_on" -> {
                // Toggle via power key through the touch service if available.
                // Full screen-off-while-streaming needs device policy; the
                // power dialog toggle keeps the stream alive on most devices.
                TouchService.instance?.pressPower()
                    ?: Log.w(TAG, "no touch service for $cmd")
            }
            else -> Log.w(TAG, "unknown command: $cmd")
        }
    }

    override fun onDestroy() {
        tearDown()
        StreamStats.state = "IDLE"
        StreamStats.stateDetail = ""
        Log.i(TAG, "streaming stopped")
        super.onDestroy()
    }

    /** Release pipeline resources without killing the service object. */
    private fun tearDown() {
        isRunning = false
        StreamControl.requestKeyFrame = null
        try { encoder?.stop() } catch (_: Exception) {}
        try { net?.stop() } catch (_: Exception) {}
        try { projection?.stop() } catch (_: Exception) {}
        projection = null
        encoder = null
        net = null
    }
}
