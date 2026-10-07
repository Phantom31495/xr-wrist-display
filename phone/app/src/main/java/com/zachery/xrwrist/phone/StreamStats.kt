package com.zachery.xrwrist.phone

import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.atomic.AtomicLong

/**
 * Live runtime telemetry for the streaming pipeline.
 * Updated by [NetworkServer] and [VideoEncoder]; read by the
 * developer UI and console. All fields are thread-safe.
 */
object StreamStats {

    /** Lifecycle: IDLE → STARTING → STREAMING → STOPPING → IDLE (or ERROR). */
    @Volatile var state: String = "IDLE"

    @Volatile var stateDetail: String = ""

    val bytesSent = AtomicLong(0)
    val framesEncoded = AtomicLong(0)
    val nalsSent = AtomicLong(0)
    val droppedFrames = AtomicLong(0)
    val videoClients = AtomicInteger(0)
    val controlClients = AtomicInteger(0)
    val discoveryReplies = AtomicLong(0)
    val touchEvents = AtomicLong(0)

    @Volatile var streamStartTimeMs: Long = 0
    @Volatile var lastFrameTimeMs: Long = 0
    @Volatile var lastError: String = ""
    @Volatile var lastVideoClientIp: String = ""
    @Volatile var lastControlClientIp: String = ""

    /**
     * Rolling 1-second bitrate samples (Mbps), newest last, capped at 120.
     * Fed by the network sampler thread; rendered by the bitrate graph.
     */
    private val bitrateHistory = ArrayDeque<Float>()
    private const val HISTORY_CAP = 120

    @Synchronized
    fun pushBitrateSample(mbps: Float) {
        bitrateHistory.addLast(mbps)
        while (bitrateHistory.size > HISTORY_CAP) bitrateHistory.removeFirst()
    }

    @Synchronized
    fun bitrateHistorySnapshot(): List<Float> = bitrateHistory.toList()

    fun reset() {
        bytesSent.set(0)
        framesEncoded.set(0)
        nalsSent.set(0)
        droppedFrames.set(0)
        videoClients.set(0)
        controlClients.set(0)
        discoveryReplies.set(0)
        touchEvents.set(0)
        streamStartTimeMs = 0
        lastFrameTimeMs = 0
        lastError = ""
        lastVideoClientIp = ""
        lastControlClientIp = ""
        state = "IDLE"
        stateDetail = ""
        synchronized(this) { bitrateHistory.clear() }
    }

    fun uptimeMs(): Long =
        if (streamStartTimeMs == 0L) 0 else System.currentTimeMillis() - streamStartTimeMs

    fun fpsEstimate(): Double {
        val uptimeSec = uptimeMs() / 1000.0
        return if (uptimeSec > 1.0) framesEncoded.get() / uptimeSec else 0.0
    }

    fun bitrateEstimateBps(): Double {
        val uptimeSec = uptimeMs() / 1000.0
        return if (uptimeSec > 1.0) (bytesSent.get() * 8.0) / uptimeSec else 0.0
    }

    fun summary(): String = buildString {
        appendLine("state=$state${if (stateDetail.isNotBlank()) " ($stateDetail)" else ""}")
        appendLine("uptime=${uptimeMs() / 1000}s")
        appendLine("frames=${framesEncoded.get()} (~${"%.1f".format(fpsEstimate())} fps)")
        appendLine("dropped=${droppedFrames.get()}")
        appendLine("bytes=${bytesSent.get()} (~${"%.2f".format(bitrateEstimateBps() / 1_000_000)} Mbps)")
        appendLine("nals=${nalsSent.get()}")
        appendLine("video_clients=${videoClients.get()}")
        appendLine("control_clients=${controlClients.get()}")
        appendLine("discovery_replies=${discoveryReplies.get()}")
        appendLine("touch_events=${touchEvents.get()}")
        if (lastError.isNotBlank()) appendLine("last_error=$lastError")
    }
}
