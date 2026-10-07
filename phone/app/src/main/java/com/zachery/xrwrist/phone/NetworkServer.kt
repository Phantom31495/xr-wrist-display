package com.zachery.xrwrist.phone

import android.util.Log
import org.json.JSONObject
import java.io.BufferedReader
import java.io.InputStreamReader
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.net.ServerSocket
import java.net.Socket
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Wire protocol implementation.
 *
 * Discovery: UDP 8899 — respond to any packet with
 *   {"ip":"<local-ip>","v":1,"name":"ziggy"}
 *
 * Video: TCP 8900 — client connects, sends {"hello":"xr-wrist","v":1}\n,
 *   server replies {"width":720,"height":1280,"fps":30}\n, then streams
 *   H.264 Annex B NAL units, each prefixed with 4-byte big-endian length.
 *
 * Control: TCP 8901 — newline-delimited JSON:
 *   {"t":"touch","a":"down|move|up","x":0..1,"y":0..1}
 *   {"t":"cmd","c":"screen_off|screen_on"}
 */
class NetworkServer(
    private val videoWidth: Int,
    private val videoHeight: Int,
    private val videoFps: Int,
    private val onTouch: (action: String, x: Float, y: Float) -> Unit,
    private val onCommand: (cmd: String) -> Unit
) {
    companion object {
        private const val TAG = "XRWristNet"
        const val PORT_DISCOVERY = 8899
        const val PORT_VIDEO = 8900
        const val PORT_CONTROL = 8901
    }

    private val running = AtomicBoolean(false)
    private val videoClients = CopyOnWriteArrayList<Socket>()
    private var discoverySocket: DatagramSocket? = null
    private var videoServer: ServerSocket? = null
    private var controlServer: ServerSocket? = null

    fun start() {
        if (running.getAndSet(true)) return
        StreamStats.state = "STARTING"
        StreamStats.stateDetail = "binding ports"
        Thread({ discoveryLoop() }, "XRWrist-Discovery").start()
        Thread({ videoAcceptLoop() }, "XRWrist-VideoAccept").start()
        Thread({ controlAcceptLoop() }, "XRWrist-ControlAccept").start()
        Thread({ samplerLoop() }, "XRWrist-Sampler").start()
        Log.i(TAG, "servers up: udp=$PORT_DISCOVERY tcp=$PORT_VIDEO,$PORT_CONTROL")
    }

    /** 1 Hz sampler: feeds the bitrate history used by the Godmode graph. */
    private fun samplerLoop() {
        var lastBytes = 0L
        var lastT = System.nanoTime()
        while (running.get()) {
            try {
                Thread.sleep(1000)
            } catch (_: InterruptedException) { break }
            if (!running.get()) break
            val now = System.nanoTime()
            val bytes = StreamStats.bytesSent.get()
            val dtSec = (now - lastT) / 1_000_000_000.0
            if (dtSec > 0) {
                val mbps = ((bytes - lastBytes) * 8.0 / dtSec / 1_000_000).toFloat()
                StreamStats.pushBitrateSample(mbps.coerceAtLeast(0f))
            }
            lastBytes = bytes
            lastT = now
        }
    }

    fun stop() {
        running.set(false)
        StreamStats.state = "STOPPING"
        try { discoverySocket?.close() } catch (_: Exception) {}
        try { videoServer?.close() } catch (_: Exception) {}
        try { controlServer?.close() } catch (_: Exception) {}
        videoClients.forEach { try { it.close() } catch (_: Exception) {} }
        videoClients.clear()
        StreamStats.videoClients.set(0)
        StreamStats.controlClients.set(0)
    }

    /** Broadcast by encoder thread — thread-safe. */
    fun sendVideoNal(nal: ByteArray) {
        if (videoClients.isEmpty()) return
        val header = ByteBuffer.allocate(4).order(ByteOrder.BIG_ENDIAN)
            .putInt(nal.size).array()
        val dead = mutableListOf<Socket>()
        var sentTo = 0
        for (s in videoClients) {
            try {
                val out = s.getOutputStream()
                out.write(header)
                out.write(nal)
                sentTo++
            } catch (e: Exception) {
                dead.add(s)
            }
        }
        if (sentTo > 0) {
            StreamStats.nalsSent.incrementAndGet()
            StreamStats.bytesSent.addAndGet((4L + nal.size) * sentTo)
        }
        if (dead.isNotEmpty()) {
            videoClients.removeAll(dead)
            StreamStats.videoClients.set(videoClients.size)
            dead.forEach { try { it.close() } catch (_: Exception) {} }
            Log.i(TAG, "dropped ${dead.size} dead video client(s)")
        }
    }

    private fun localIp(): String {
        // Manual override from Developer Options takes precedence.
        val override = try { DevSettings.ipOverride } catch (_: Exception) { "" }
        if (override.isNotBlank()) return override
        return try {
            val interfaces = java.util.Collections.list(
                java.net.NetworkInterface.getNetworkInterfaces())
            for (ni in interfaces) {
                val addrs = java.util.Collections.list(ni.inetAddresses)
                for (a in addrs) {
                    if (!a.isLoopbackAddress && a is java.net.Inet4Address) {
                        val ip = a.hostAddress ?: continue
                        if (ip.startsWith("192.168.") || ip.startsWith("10.")) return ip
                    }
                }
            }
            "0.0.0.0"
        } catch (e: Exception) {
            "0.0.0.0"
        }
    }

    private fun discoveryLoop() {
        try {
            val sock = DatagramSocket(PORT_DISCOVERY).apply {
                broadcast = true
                soTimeout = 2000
            }
            discoverySocket = sock
            val buf = ByteArray(256)
            while (running.get()) {
                try {
                    val pkt = DatagramPacket(buf, buf.size)
                    sock.receive(pkt)
                    val reply = JSONObject()
                        .put("ip", localIp())
                        .put("v", 1)
                        .put("name", try { DevSettings.discoveryName } catch (_: Exception) { "ziggy" })
                        .toString()
                    val data = reply.toByteArray()
                    sock.send(DatagramPacket(data, data.size, pkt.address, pkt.port))
                    StreamStats.discoveryReplies.incrementAndGet()
                    if (DevSettings.verboseLogging) {
                        Log.d(TAG, "discovery reply to ${pkt.address.hostAddress}")
                    }
                } catch (_: java.net.SocketTimeoutException) {
                    // loop again
                } catch (e: Exception) {
                    if (running.get()) Log.w(TAG, "discovery error", e)
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "discovery loop died", e)
        }
    }

    private fun videoAcceptLoop() {
        try {
            val server = ServerSocket(PORT_VIDEO)
            videoServer = server
            while (running.get()) {
                try {
                    val client = server.accept()
                    Log.i(TAG, "video client: ${client.inetAddress.hostAddress}")
                    Thread({ handleVideoClient(client) },
                        "XRWrist-VideoClient").start()
                } catch (e: Exception) {
                    if (running.get()) Log.w(TAG, "video accept error", e)
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "video accept loop died", e)
        }
    }

    private fun handleVideoClient(sock: Socket) {
        try {
            sock.soTimeout = 10_000
            val reader = BufferedReader(InputStreamReader(sock.getInputStream()))
            val helloLine = reader.readLine()
            Log.d(TAG, "video hello: $helloLine")
            val hello = try { JSONObject(helloLine) } catch (_: Exception) { null }
            if (hello?.optString("hello") != "xr-wrist") {
                Log.w(TAG, "bad hello, closing")
                sock.close()
                return
            }
            val params = JSONObject()
                .put("width", videoWidth)
                .put("height", videoHeight)
                .put("fps", videoFps)
                .toString() + "\n"
            sock.getOutputStream().write(params.toByteArray())
            sock.getOutputStream().flush()
            sock.soTimeout = 0
            videoClients.add(sock)
            StreamStats.videoClients.set(videoClients.size)
            StreamStats.lastVideoClientIp = sock.inetAddress.hostAddress ?: ""
            StreamStats.state = "STREAMING"
            StreamStats.stateDetail = "video client connected"
            Log.i(TAG, "video streaming to ${sock.inetAddress.hostAddress}")
            // Hold the connection; server pushes NALs via sendVideoNal.
            // Read until EOF to detect disconnect.
            try {
                while (running.get() && reader.read() != -1) { /* discard */ }
            } catch (_: Exception) { }
        } catch (e: Exception) {
            Log.w(TAG, "video client error", e)
        } finally {
            videoClients.remove(sock)
            StreamStats.videoClients.set(videoClients.size)
            try { sock.close() } catch (_: Exception) {}
            Log.i(TAG, "video client disconnected")
        }
    }

    private fun controlAcceptLoop() {
        try {
            val server = ServerSocket(PORT_CONTROL)
            controlServer = server
            while (running.get()) {
                try {
                    val client = server.accept()
                    Log.i(TAG, "control client: ${client.inetAddress.hostAddress}")
                    Thread({ handleControlClient(client) },
                        "XRWrist-ControlClient").start()
                } catch (e: Exception) {
                    if (running.get()) Log.w(TAG, "control accept error", e)
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "control accept loop died", e)
        }
    }

    private fun handleControlClient(sock: Socket) {
        StreamStats.controlClients.incrementAndGet()
        StreamStats.lastControlClientIp = sock.inetAddress.hostAddress ?: ""
        try {
            val reader = BufferedReader(InputStreamReader(sock.getInputStream()))
            while (running.get()) {
                val line = reader.readLine() ?: break
                try {
                    val msg = JSONObject(line)
                    when (msg.optString("t")) {
                        "touch" -> {
                            val a = msg.optString("a")
                            val x = msg.optDouble("x", 0.5).toFloat()
                            val y = msg.optDouble("y", 0.5).toFloat()
                            StreamStats.touchEvents.incrementAndGet()
                            onTouch(a, x, y)
                        }
                        "cmd" -> onCommand(msg.optString("c"))
                        else -> Log.w(TAG, "unknown control msg: $line")
                    }
                } catch (e: Exception) {
                    Log.w(TAG, "bad control JSON: $line", e)
                }
            }
        } catch (e: Exception) {
            Log.w(TAG, "control client error", e)
        } finally {
            StreamStats.controlClients.decrementAndGet()
            try { sock.close() } catch (_: Exception) {}
            Log.i(TAG, "control client disconnected")
        }
    }
}
