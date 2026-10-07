package com.zachery.xrwrist.phone

import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import android.util.Log
import android.view.Surface
import java.nio.ByteBuffer

/**
 * H.264 encoder fed by a Surface (from MediaProjection VirtualDisplay).
 * Emits Annex B NAL units via onNal callback.
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
        private const val TIMEOUT_US = 10_000L
    }

    private var codec: MediaCodec? = null
    private var running = false
    private var encodeThread: Thread? = null
    private var cachedSurface: Surface? = null
    private var lastPtsUs: Long = -1L

    val inputSurface: Surface?
        get() = cachedSurface

    fun start() {
        val format = MediaFormat.createVideoFormat(MediaFormat.MIMETYPE_VIDEO_AVC, width, height).apply {
            setInteger(MediaFormat.KEY_COLOR_FORMAT,
                MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface)
            setInteger(MediaFormat.KEY_BIT_RATE, bitrate)
            setInteger(MediaFormat.KEY_FRAME_RATE, fps)
            setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 2)
            setInteger(MediaFormat.KEY_REPEAT_PREVIOUS_FRAME_AFTER, 1_000_000 / fps)
        }

        val c = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_VIDEO_AVC)
        c.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
        // Create input surface BEFORE start(); cache it for the service
        cachedSurface = c.createInputSurface()
        c.start()
        codec = c
        running = true

        encodeThread = Thread({
            val bufferInfo = MediaCodec.BufferInfo()
            // Annex B start code
            val startCode = byteArrayOf(0, 0, 0, 1)
            while (running) {
                try {
                    val idx = c.dequeueOutputBuffer(bufferInfo, TIMEOUT_US)
                    if (idx >= 0) {
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
                        c.releaseOutputBuffer(idx, false)
                    } else if (idx == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                        Log.i(TAG, "format: ${c.outputFormat}")
                    }
                } catch (e: Exception) {
                    if (running) Log.e(TAG, "encode loop error", e)
                }
            }
        }, "XRWrist-Encode").apply { start() }

        Log.i(TAG, "encoder started ${width}x${height}@${fps}")
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
        try { encodeThread?.join(1000) } catch (_: Exception) {}
        try { codec?.stop() } catch (_: Exception) {}
        try { codec?.release() } catch (_: Exception) {}
        codec = null
        cachedSurface = null
        Log.i(TAG, "encoder stopped")
    }
}
