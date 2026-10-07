package com.zachery.xrwrist.phone

import android.util.Log
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.Socket

/**
 * Network diagnostics for the Godmode section.
 *
 * Everything here uses unprivileged APIs: the system ping binary,
 * TCP connect timing, and TTL-limited probes. No root required.
 * All blocking calls — run off the main thread.
 */
object NetDiag {

    private const val TAG = "XRWristDiag"

    data class PingResult(
        val host: String,
        val ok: Boolean,
        val avgMs: Double,
        val transmitted: Int,
        val received: Int,
        val raw: String
    )

    /** ICMP ping via the platform ping binary (toybox). Falls back to isReachable. */
    fun ping(host: String, count: Int = 4, timeoutSec: Int = 2): PingResult {
        val clean = host.trim()
        if (clean.isEmpty()) return PingResult(host, false, -1.0, 0, 0, "empty host")
        return try {
            val proc = Runtime.getRuntime().exec(
                arrayOf("/system/bin/ping", "-c", count.toString(),
                    "-W", timeoutSec.toString(), clean))
            val out = proc.inputStream.bufferedReader().readText()
            val err = proc.errorStream.bufferedReader().readText()
            proc.waitFor()
            val text = (out + err).trim()
            val txRx = Regex("""(\d+) packets transmitted, (\d+) (packets |)received""")
                .find(text)
            val tx = txRx?.groupValues?.get(1)?.toIntOrNull() ?: 0
            val rx = txRx?.groupValues?.get(2)?.toIntOrNull() ?: 0
            val avg = Regex("""(?:rtt|round-trip)[^\n=]*=\s*[\d.]+/([\d.]+)/""")
                .find(text)?.groupValues?.get(1)?.toDoubleOrNull() ?: -1.0
            PingResult(clean, rx > 0, avg, tx, rx,
                text.ifBlank { "(no ping output — trying Java fallback)" }
                    .let { if (rx == 0) javaFallback(clean, timeoutSec * 1000) else it })
        } catch (e: Exception) {
            Log.w(TAG, "ping exec failed", e)
            val fb = javaFallback(clean, timeoutSec * 1000)
            PingResult(clean, fb.startsWith("reachable"), -1.0, 0, 0, fb)
        }
    }

    private fun javaFallback(host: String, timeoutMs: Int): String {
        return try {
            val t0 = System.nanoTime()
            val ok = InetAddress.getByName(host).isReachable(timeoutMs)
            val ms = (System.nanoTime() - t0) / 1_000_000.0
            if (ok) "reachable via InetAddress.isReachable in ${"%.1f".format(ms)} ms"
            else "unreachable (isReachable timed out after ${timeoutMs}ms)"
        } catch (e: Exception) {
            "fallback failed: ${e.message}"
        }
    }

    /** TCP connect latency to a single port. */
    fun tcpLatency(host: String, port: Int, timeoutMs: Int = 3000): String {
        return try {
            val t0 = System.nanoTime()
            Socket().use { s ->
                s.tcpNoDelay = true
                s.connect(InetSocketAddress(host, port), timeoutMs)
            }
            val ms = (System.nanoTime() - t0) / 1_000_000.0
            "$host:$port OPEN — connect ${"%.1f".format(ms)} ms"
        } catch (e: Exception) {
            "$host:$port CLOSED/unreachable (${e.message?.take(80)})"
        }
    }

    /** Check the three XR Wrist protocol ports on the Quest. */
    fun questPortScan(host: String): String = buildString {
        appendLine("port scan → $host")
        appendLine("udp/${NetworkServer.PORT_DISCOVERY} discovery — ${udpProbe(host)}")
        appendLine("tcp/${NetworkServer.PORT_VIDEO} video — ${tcpLatency(host, NetworkServer.PORT_VIDEO)}")
        appendLine("tcp/${NetworkServer.PORT_CONTROL} control — ${tcpLatency(host, NetworkServer.PORT_CONTROL)}")
    }.trimEnd()

    private fun udpProbe(host: String): String {
        return try {
            java.net.DatagramSocket().use { sock ->
                sock.soTimeout = 2000
                val probe = "xr-wrist-ping".toByteArray()
                val addr = InetAddress.getByName(host)
                sock.send(java.net.DatagramPacket(probe, probe.size, addr,
                    NetworkServer.PORT_DISCOVERY))
                val buf = ByteArray(512)
                val resp = java.net.DatagramPacket(buf, buf.size)
                val t0 = System.nanoTime()
                sock.receive(resp)
                val ms = (System.nanoTime() - t0) / 1_000_000.0
                val body = String(resp.data, 0, resp.length).take(120)
                "OPEN — reply in ${"%.1f".format(ms)} ms: $body"
            }
        } catch (e: Exception) {
            "no reply (${e.message?.take(60)})"
        }
    }

    /**
     * Best-effort traceroute using TTL-limited ICMP probes via the
     * system ping binary (`ping -t <ttl> -c 1`). Each hop that sends
     * back ICMP Time Exceeded shows up as "From <ip>".
     */
    fun traceroute(host: String, maxTtl: Int = 12, timeoutSec: Int = 2): String {
        val clean = host.trim()
        if (clean.isEmpty()) return "empty host"
        val sb = StringBuilder()
        sb.appendLine("traceroute → $clean (max $maxTtl hops)")
        val hopIp = Regex("""From ([0-9a-fA-F.:]+)""")
        val rtt = Regex("""time=([\d.]+)\s*ms""")
        for (ttl in 1..maxTtl) {
            try {
                val proc = Runtime.getRuntime().exec(
                    arrayOf("/system/bin/ping", "-t", ttl.toString(),
                        "-c", "1", "-W", timeoutSec.toString(), clean))
                val out = proc.inputStream.bufferedReader().readText() +
                    proc.errorStream.bufferedReader().readText()
                proc.waitFor()
                val hop = hopIp.find(out)?.groupValues?.get(1)
                val ms = rtt.find(out)?.groupValues?.get(1)
                if (hop != null) {
                    sb.appendLine(" $ttl  $hop  ${ms?.let { "$it ms" } ?: ""}")
                    if (hop == clean) break
                } else if (out.contains("1 packets transmitted, 1 received") ||
                    out.contains("1 received")) {
                    sb.appendLine(" $ttl  $clean  ${ms?.let { "$it ms" } ?: ""}  ← target")
                    break
                } else {
                    sb.appendLine(" $ttl  *")
                }
            } catch (e: Exception) {
                sb.appendLine(" $ttl  error: ${e.message?.take(50)}")
            }
        }
        return sb.toString().trimEnd()
    }
}
