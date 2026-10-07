package com.zachery.xrwrist.phone

import android.os.Handler
import android.os.Looper
import java.io.BufferedReader
import java.io.InputStreamReader
import java.io.OutputStreamWriter

/**
 * Persistent interactive shell running as the app's own UID (no root).
 * A single /system/bin/sh process lives for the session; stdin/stdout are
 * pumped on background threads. `cd` works naturally — it's one process.
 */
class TerminalSession(private val onOutput: (String) -> Unit) {

    private val ui = Handler(Looper.getMainLooper())
    private val lock = Any()
    private var proc: Process? = null
    private var writer: OutputStreamWriter? = null

    private fun alive(): Boolean = proc?.let {
        try { it.exitValue(); false } catch (_: IllegalThreadStateException) { true }
    } ?: false

    fun start() {
        synchronized(lock) {
            if (alive()) return
            try {
                val p = ProcessBuilder("/system/bin/sh").start()
                proc = p
                writer = OutputStreamWriter(p.outputStream, Charsets.UTF_8)
                pump(p.inputStream)
                pump(p.errorStream)
                emit("[shell ready — app sandbox uid, no root]\n")
            } catch (e: Exception) {
                emit("[shell failed: ${e.message}]\n")
            }
        }
    }

    private fun pump(stream: java.io.InputStream) {
        Thread({
            try {
                BufferedReader(InputStreamReader(stream, Charsets.UTF_8)).use { r ->
                    var line: String?
                    while (r.readLine().also { line = it } != null) {
                        emit((line ?: break) + "\n")
                    }
                }
            } catch (_: Exception) { /* stream closed */ }
        }, "term-pump").apply { isDaemon = true; start() }
    }

    private fun emit(s: String) = ui.post { onOutput(s) }

    fun send(cmd: String) {
        val c = cmd.trim()
        if (c.isEmpty()) return
        synchronized(lock) {
            if (!alive()) start()
            try {
                emit("$ $c\n")
                writer?.apply { write(c + "\n"); flush() }
            } catch (e: Exception) {
                emit("[write failed: ${e.message} — restarting]\n")
                destroy(); start()
            }
        }
    }

    fun restart() {
        synchronized(lock) { destroy() }
        emit("[shell restarted]\n")
        start()
    }

    fun destroy() {
        synchronized(lock) {
            try { writer?.close() } catch (_: Exception) { }
            try { proc?.destroy() } catch (_: Exception) { }
            proc = null; writer = null
        }
    }
}
