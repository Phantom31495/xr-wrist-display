package com.zachery.xrwrist.phone

import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import android.os.Build
import android.os.Looper
import android.os.SystemClock
import android.util.Log
import android.view.Surface
import java.nio.ByteBuffer

/**
 * H.264 encoder fed by a Surface (from MediaProjection VirtualDisplay).
 * Emits Annex B NAL units via onNal callback.
 *
 * Encoder tuning follows scrcpy's battle-tested configuration
 * (Genymobile/scrcpy, SurfaceEncoder.java):
 *  - KEY_FRAME_RATE=60 as a formality (actual rate is variable)
 *  - 10s I-frame interval (fewer keyframes = lower bitrate)
 *  - KEY_PRIORITY=0 (realtime) on API 23+
 *  - KEY_LATENCY=1 (emit frame ASAP) on API 26+
 *  - "max-fps-to-encoder" private key caps the real encode rate
 *  - Blocking dequeue (no busy-wait polling)
 *  - Codec-config packets handled separately, not forwarded as video
 *  - Consecutive-error tracking with brief backoff before giving up
 */
class VideoEncoder(
    private val width: Int,
    private val height: Int,
    private val fps: Int,
    private val bitrate: Int,
    private val onNal: (ByteArray) -> Unit
) {
    companion object {
        private const val TAG = "XRWristEncoder"

        // scrcpy: I-frame every 10s. Fewer keyframes = steadier bitrate,
        // lower latency spikes. Keyframes are also requestable on demand.
        private const val I_FRAME_INTERVAL_SEC = 10

        // scrcpy: repeat the previous frame after 100ms of no new frames,
        // so the very first frame displays and quality recovers on stalls.
        private const val REPEAT_FRAME_DELAY_US = 100_000L

        // Private (undocumented) key scrcpy uses to cap the encoder's
        // real frame rate. Present in AOSP since Android 10.
        private const val KEY_MAX_FPS_TO_ENCODER = "max-fps-to-encoder"

        private const val MAX_CONSECUTIVE_ERRORS = 3
        private const val RETRY_BACKOFF_MS = 50L
    }

    private var codec: MediaCodec? = null
    private var running = false
    private var encodeThread: Thread? = null
    private var cachedSurface: Surface? = null
    private var lastPtsUs: Long = -1L
    private var consecutiveErrors = 0
    private var firstFrameSent = false

    val inputSurface: Surface?
        get() = cachedSurface

    fun start() {
        val format = MediaFormat.createVideoFormat(MediaFormat.MIMETYPE_VIDEO_AVC, width, height).apply {
            setInteger(MediaFormat.KEY_COLOR_FORMAT,
                MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface)
            setInteger(MediaFormat.KEY_BIT_RATE, bitrate)
            // scrcpy: 60fps here is a formality; the real rate is variable
            // and capped by max-fps-to-encoder below.
            setInteger(MediaFormat.KEY_FRAME_RATE, 60)
            setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, I_FRAME_INTERVAL_SEC)
            setLong(MediaFormat.KEY_REPEAT_PREVIOUS_FRAME_AFTER, REPEAT_FRAME_DELAY_US)
            if (Build.VERSION.SDK_INT >= 24) {
                setInteger(MediaFormat.KEY_COLOR_RANGE, MediaFormat.COLOR_RANGE_LIMITED)
            }
            if (Build.VERSION.SDK_INT >= 23) {
                // scrcpy: realtime priority for the encoder thread
                setInteger(MediaFormat.KEY_PRIORITY, 0)
            }
            if (Build.VERSION.SDK_INT >= 26) {
                // scrcpy: output 1 frame as soon as 1 frame is queued.
                // This is the single biggest latency win.
                setInteger(MediaFormat.KEY_LATENCY, 1)
            }
            // scrcpy: cap the actual encode rate (private AOSP key)
            setFloat(KEY_MAX_FPS_TO_ENCODER, fps.toFloat())
        }

        val c = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_VIDEO_AVC)
        c.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
        // Create input surface BEFORE start(); cache it for the service
        cachedSurface = c.createInputSurface()
        c.start()
        codec = c
        running = true
        consecutiveErrors = 0
        firstFrameSent = false

        encodeThread = Thread({
            // scrcpy: some devices (Meizu) deadlock if the encode thread
            // has no Looper.
            Looper.prepare()

            val bufferInfo = MediaCodec.BufferInfo()
            // Annex B start code
            val startCode = byteArrayOf(0, 0, 0, 1)
            while (running) {
                try {
                    // scrcpy: block indefinitely instead of polling with a
                    // timeout — no busy-wait, lowest wakeup latency.
                    val idx = c.dequeueOutputBuffer(bufferInfo, -1)
                    if (idx >= 0) {
                        val isConfig = (bufferInfo.flags and
                            MediaCodec.BUFFER_FLAG_CODEC_CONFIG) != 0
                        if (isConfig) {
                            // scrcpy: codec-config (SPS/PPS) packets are not
                            // video frames. Our Quest decoder gets SPS/PPS
                            // from the first keyframe's Annex B stream, so
                            // we skip config packets here instead of
                            // forwarding them as video.
                            if (DevSettings.verboseLogging) {
                                Log.d(TAG, "codec config packet (${bufferInfo.size}b), skipped")
                            }
                        } else if (bufferInfo.size > 0) {
                            val buf: ByteBuffer = c.getOutputBuffer(idx)!!
                            val data = ByteArray(bufferInfo.size)
                            buf.get(data)
                            // Dropped-frame estimate: a PTS gap far beyond the
                            // frame interval means the encoder skipped frames.
                            val pts = bufferInfo.presentationTimeUs
                            if (lastPtsUs >= 0 && pts > lastPtsUs) {
                                val intervalUs = 1_000_000L / fps.coerceAtLeast(1)
                                val gap = pts - lastPtsUs
                                if (gap > intervalUs * 3 / 2) {
                                    val missed = ((gap - intervalUs / 2) / intervalUs)
                                        .coerceAtLeast(1)
                                    StreamStats.droppedFrames.addAndGet(missed)
                                }
                            }
                            if (pts > lastPtsUs) lastPtsUs = pts
                            // Prepend Annex B start code (encoder emits raw NALs)
                            val nal = startCode + data
                            StreamStats.framesEncoded.incrementAndGet()
                            StreamStats.lastFrameTimeMs = System.currentTimeMillis()
                            onNal(nal)
                            firstFrameSent = true
                            consecutiveErrors = 0
                        }
                        c.releaseOutputBuffer(idx, false)
                    } else if (idx == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                        Log.i(TAG, "format: ${c.outputFormat}")
                    }
                } catch (e: Exception) {
                    if (!running) break
                    // scrcpy-style: brief backoff, then give up after
                    // too many consecutive failures.
                    consecutiveErrors++
                    Log.w(TAG, "encode loop error ($consecutiveErrors/$MAX_CONSECUTIVE_ERRORS)", e)
                    if (consecutiveErrors >= MAX_CONSECUTIVE_ERRORS) {
                        Log.e(TAG, "encoder failing repeatedly; stopping")
                        StreamStats.lastError = "video encoder failed repeatedly"
                        StreamStats.state = "ERROR"
                        break
                    }
                    SystemClock.sleep(RETRY_BACKOFF_MS)
                }
            }
        }, "XRWrist-Encode").apply { start() }

        Log.i(TAG, "encoder started ${width}x${height}@${fps} " +
            "(scrcpy-tuned: iframe=${I_FRAME_INTERVAL_SEC}s, latency=1, priority=realtime)")
    }

    fun requestKeyFrame() {
        try {
            val b = android.os.Bundle()
            b.putInt(MediaCodec.PARAMETER_KEY_REQUEST_SYNC_FRAME, 0)
            codec?.setParameters(b)
        } catch (e: Exception) {
            Log.w(TAG, "keyframe request failed", e)
        }
    }

    fun stop() {
        running = false
        try {
            // Interrupt the blocking dequeue by stopping the codec first;
            // the thread will exit its loop on the resulting exception.
            codec?.let {
                try { it.signalEndOfInputStream() } catch (_: Exception) {}
            }
        } catch (_: Exception) {}
        try { encodeThread?.join(2000) } catch (_: Exception) {}
        try { codec?.stop() } catch (_: Exception) {}
        try { codec?.release() } catch (_: Exception) {}
        codec = null
        cachedSurface = null
        Log.i(TAG, "encoder stopped")
    }
}
