package com.zachery.xrwrist.phone

import android.app.Activity
import android.app.AlertDialog
import android.content.Intent
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.text.InputType
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.view.inputmethod.EditorInfo
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * Dev console — three tabs, dark terminal aesthetic, M3 accents.
 *
 *  CONSOLE  — app command console (stream control, tuning, net diag).
 *  TERMINAL — real persistent /system/bin/sh as the app uid (no root).
 *  INSPECT  — browser-inspector-style tools: view tree, tap-to-pick,
 *             app info, permissions, logcat.
 */
class ConsoleActivity : Activity() {

    private val ui = Handler(Looper.getMainLooper())
    private val timeFmt = SimpleDateFormat("HH:mm:ss", Locale.US)

    private lateinit var tabBar: LinearLayout
    private lateinit var tabConsole: LinearLayout
    private lateinit var tabTerminal: LinearLayout
    private lateinit var tabInspect: LinearLayout
    private val tabButtons = mutableListOf<Button>()
    private val tabs = mutableListOf<LinearLayout>()
    private var currentTab = 0

    // console tab
    private lateinit var conOut: TextView
    private lateinit var conScroll: ScrollView
    private lateinit var conInput: EditText
    private val history = mutableListOf<String>()

    // terminal tab
    private lateinit var termOut: TextView
    private lateinit var termScroll: ScrollView
    private lateinit var termInput: EditText
    private var termSession: TerminalSession? = null
    private var termStarted = false

    // inspect tab
    private lateinit var insOut: TextView
    private lateinit var insScroll: ScrollView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        DevSettings.init(this)

        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(Color.parseColor("#0B0B0F"))
        }

        val title = TextView(this).apply {
            text = "◤ XR WRIST · DEV CONSOLE"
            textSize = 15f
            typeface = Typeface.create("monospace", Typeface.BOLD)
            setTextColor(Color.parseColor("#D0BCFF"))
            setPadding(dp(14), dp(14), dp(14), dp(4))
        }
        root.addView(title)

        tabBar = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            setPadding(dp(14), dp(6), dp(14), dp(8))
        }
        root.addView(tabBar)

        tabConsole = buildConsoleTab()
        tabTerminal = buildTerminalTab()
        tabInspect = buildInspectTab()
        tabs.addAll(listOf(tabConsole, tabTerminal, tabInspect))
        val names = listOf("CONSOLE", "TERMINAL", "INSPECT")
        names.forEachIndexed { i, n ->
            val b = Button(this).apply {
                text = n
                textSize = 12f
                isAllCaps = false
                stateListAnimator = null
                setOnClickListener { selectTab(i) }
            }
            tabButtons.add(b)
            tabBar.addView(b, LinearLayout.LayoutParams(0,
                LinearLayout.LayoutParams.WRAP_CONTENT, 1f).apply {
                if (i > 0) leftMargin = dp(8)
            })
        }
        tabs.forEach { root.addView(it, LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f)) }

        setContentView(root)
        selectTab(0)
        conPrint("XR Wrist dev console v1.3 — console · terminal · inspect.")
        if (DevSettings.godmodeUnlocked) conPrint("godmode active. try 'matrix'.")
    }

    private fun selectTab(i: Int) {
        currentTab = i
        tabs.forEachIndexed { idx, t -> t.visibility = if (idx == i) View.VISIBLE else View.GONE }
        tabButtons.forEachIndexed { idx, b ->
            if (idx == i) {
                b.setTextColor(Color.parseColor("#381E72"))
                b.background = rounded("#D0BCFF", 12f)
            } else {
                b.setTextColor(Color.parseColor("#9A9AA5"))
                b.background = rounded("#1A1A20", 12f)
            }
        }
        if (i == 1 && !termStarted) {
            termStarted = true
            termSession = TerminalSession { s -> termAppend(s) }.also { it.start() }
        }
    }

    override fun onDestroy() {
        termSession?.destroy()
        ViewInspector.clearHighlight()
        super.onDestroy()
    }

    // ================= CONSOLE TAB =================

    private fun buildConsoleTab(): LinearLayout {
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(14), 0, dp(14), dp(14))
        }
        conOut = TextView(this).apply {
            typeface = Typeface.MONOSPACE
            textSize = 12.5f
            setTextColor(Color.parseColor("#7DD8A8"))
            setLineSpacing(dp(3).toFloat(), 1f)
        }
        conScroll = ScrollView(this).apply {
            background = rounded("#101014", 16f)
            setPadding(dp(12), dp(12), dp(12), dp(12))
            addView(conOut)
        }
        root.addView(conScroll, LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f))
        root.addView(inputRow("type 'help'", ::runConsoleCommand),
            LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT))
        // keep refs for the shared input-row builder
        conInput = lastInput
        return root
    }

    private lateinit var lastInput: EditText

    private fun inputRow(hint: String, onRun: () -> Unit): LinearLayout {
        val row = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setPadding(0, dp(10), 0, 0)
        }
        row.addView(TextView(this).apply {
            text = "❯ "
            typeface = Typeface.MONOSPACE
            textSize = 16f
            setTextColor(Color.parseColor("#D0BCFF"))
        })
        lastInput = EditText(this).apply {
            typeface = Typeface.MONOSPACE
            textSize = 14f
            setTextColor(Color.WHITE)
            setHintTextColor(Color.parseColor("#4A4A52"))
            this.hint = hint
            inputType = InputType.TYPE_CLASS_TEXT
            imeOptions = EditorInfo.IME_ACTION_SEND
            background = rounded("#1A1A20", 14f)
            setPadding(dp(14), dp(12), dp(14), dp(12))
            setOnEditorActionListener { _, actionId, _ ->
                if (actionId == EditorInfo.IME_ACTION_SEND) { onRun(); true } else false
            }
        }
        row.addView(lastInput, LinearLayout.LayoutParams(0,
            LinearLayout.LayoutParams.WRAP_CONTENT, 1f))
        val runBtn = Button(this).apply {
            text = "RUN"
            textSize = 13f
            isAllCaps = false
            setTextColor(Color.parseColor("#381E72"))
            background = rounded("#D0BCFF", 14f)
            stateListAnimator = null
            setPadding(dp(16), dp(10), dp(16), dp(10))
            setOnClickListener { onRun() }
        }
        runBtn.layoutParams = LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.WRAP_CONTENT,
            LinearLayout.LayoutParams.WRAP_CONTENT).apply {
            setMargins(dp(8), 0, 0, 0)
        }
        row.addView(runBtn)
        return row
    }

    private fun runConsoleCommand() {
        val line = conInput.text.toString().trim()
        conInput.text.clear()
        if (line.isEmpty()) return
        history.add(line)
        conPrint("❯ $line", echo = true)
        try { execute(line) } catch (e: Exception) { conPrint("ERROR: ${e.message}") }
    }

    private fun execute(line: String) {
        val parts = line.split("\\s+".toRegex())
        when (parts[0].lowercase(Locale.US)) {
            "help" -> conPrint(HELP)
            "status" -> conPrint(StreamStats.summary())
            "settings" -> conPrint(DevSettings.summary())
            "start" -> {
                if (StreamService.isRunning) conPrint("already streaming")
                else if (!ProjectionHolder.hasGrant)
                    conPrint("no capture grant — 'Start Streaming' on the main screen first")
                else {
                    startForegroundService(Intent(this, StreamService::class.java).apply {
                        action = StreamService.ACTION_RESTART
                    })
                    conPrint("stream starting…")
                }
            }
            "stop" -> {
                stopService(Intent(this, StreamService::class.java))
                conPrint("stream stopping…")
            }
            "restart" -> {
                if (!ProjectionHolder.hasGrant) conPrint("no capture grant stored")
                else {
                    startForegroundService(Intent(this, StreamService::class.java).apply {
                        action = StreamService.ACTION_RESTART
                    })
                    conPrint("stream restarting…")
                }
            }
            "clients" -> conPrint(
                "video=${StreamStats.videoClients.get()} " +
                    "(${StreamStats.lastVideoClientIp.ifBlank { "—" }})  " +
                "control=${StreamStats.controlClients.get()} " +
                    "(${StreamStats.lastControlClientIp.ifBlank { "—" }})")
            "bitrate" -> {
                if (parts.size < 2) conPrint("bitrate=${DevSettings.videoBitrate}  (usage: bitrate <bps>)")
                else {
                    val v = parts[1].toIntOrNull()
                    if (v == null || v !in 500_000..20_000_000) conPrint("range: 500000–20000000")
                    else { DevSettings.videoBitrate = v; conPrint("bitrate=$v — restart stream to apply") }
                }
            }
            "res" -> {
                if (parts.size < 3) conPrint("res=${DevSettings.videoWidth}x${DevSettings.videoHeight}  (usage: res <w> <h>)")
                else {
                    val w = parts[1].toIntOrNull(); val h = parts[2].toIntOrNull()
                    if (w == null || h == null) conPrint("need two numbers")
                    else { DevSettings.videoWidth = w; DevSettings.videoHeight = h
                        conPrint("res=${DevSettings.videoWidth}x${DevSettings.videoHeight} — restart to apply") }
                }
            }
            "fps" -> {
                if (parts.size < 2) conPrint("fps=${DevSettings.videoFps}  (usage: fps <15–60>)")
                else {
                    val v = parts[1].toIntOrNull()
                    if (v == null) conPrint("need a number")
                    else { DevSettings.videoFps = v; conPrint("fps=${DevSettings.videoFps} — restart to apply") }
                }
            }
            "ip" -> {
                if (parts.size < 2)
                    conPrint("ip_override=${DevSettings.ipOverride.ifBlank { "(auto)" }}  (usage: ip <addr> | ip clear)")
                else if (parts[1].lowercase() == "clear") { DevSettings.ipOverride = ""; conPrint("ip override cleared") }
                else { DevSettings.ipOverride = parts[1]; conPrint("ip_override=${DevSettings.ipOverride}") }
            }
            "verbose" -> {
                if (parts.size < 2) conPrint("verbose=${DevSettings.verboseLogging}  (usage: verbose <on|off>)")
                else {
                    DevSettings.verboseLogging = parts[1].lowercase() in setOf("on", "true", "1")
                    conPrint("verbose=${DevSettings.verboseLogging}")
                }
            }
            "keyframe" -> conPrint(
                if (StreamControl.keyFrame()) "IDR keyframe requested" else "no active encoder")
            "ping" -> {
                val host = parts.getOrElse(1) {
                    StreamStats.lastVideoClientIp.ifBlank { DevSettings.ipOverride } }
                if (host.isBlank()) conPrint("usage: ping <host>")
                else {
                    conPrint("pinging $host …")
                    Thread {
                        val r = NetDiag.ping(host)
                        ui.post {
                            conPrint(if (r.ok)
                                "pong ${r.host}: ${"%.1f".format(r.avgMs)} ms avg (${r.received}/${r.transmitted})"
                            else "ping failed: ${r.raw.take(200)}")
                        }
                    }.start()
                }
            }
            "tcping" -> {
                if (parts.size < 3) conPrint("usage: tcping <host> <port>")
                else {
                    val host = parts[1]; val port = parts[2].toIntOrNull() ?: -1
                    conPrint("probing $host:$port …")
                    Thread {
                        val r = NetDiag.tcpLatency(host, port)
                        ui.post { conPrint(r) }
                    }.start()
                }
            }
            "diag" -> {
                conPrint("running diagnostics (encoder probe takes a moment)…")
                Thread {
                    val r = Compat.runDiagnostics(this)
                    ui.post { conPrint(r) }
                }.start()
            }
            // cross-links into the new tabs
            "sh" -> { selectTab(1); termAppend("[jumped from console — type shell commands]\n") }
            "inspect" -> selectTab(2)
            "tree" -> { selectTab(2); dumpTree() }
            "godmode" -> { DevSettings.godmodeUnlocked = true; conPrint("⚡ GODMODE UNLOCKED — check Dev Options") }
            "matrix" -> conPrint(MATRIX)
            "sudo" -> conPrint("nice try. terminal tab has a real shell — still no root, by design.")
            "tips" -> conPrint(TIPS)
            "clear" -> conOut.text = ""
            "whoami" -> conPrint("com.zachery.xrwrist.phone " +
                packageManager.getPackageInfo(packageName, 0).versionName +
                " — " + Compat.deviceLine())
            "log" -> conPrint("use Dev Options → Export logs for a full tag dump")
            else -> conPrint("unknown '${parts[0]}' — 'help' lists commands")
        }
    }

    private fun conPrint(msg: String, echo: Boolean = false) {
        val line = if (echo) msg else "[${timeFmt.format(Date())}] $msg"
        conOut.append(line + "\n")
        conScroll.post { conScroll.fullScroll(ScrollView.FOCUS_DOWN) }
    }

    // ================= TERMINAL TAB =================

    private fun buildTerminalTab(): LinearLayout {
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(14), 0, dp(14), dp(14))
        }
        val header = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setPadding(0, 0, 0, dp(6))
        }
        header.addView(TextView(this).apply {
            text = "app-uid shell · /system/bin/sh"
            typeface = Typeface.MONOSPACE
            textSize = 11f
            setTextColor(Color.parseColor("#4A4A52"))
        }, LinearLayout.LayoutParams(0,
            LinearLayout.LayoutParams.WRAP_CONTENT, 1f))
        val rst = Button(this).apply {
            text = "RST"
            textSize = 11f
            isAllCaps = false
            setTextColor(Color.parseColor("#9A9AA5"))
            background = rounded("#1A1A20", 10f)
            stateListAnimator = null
            setOnClickListener { termSession?.restart() }
        }
        header.addView(rst)
        root.addView(header)

        termOut = TextView(this).apply {
            typeface = Typeface.MONOSPACE
            textSize = 12f
            setTextColor(Color.parseColor("#E8E8EC"))
            setLineSpacing(dp(2).toFloat(), 1f)
            setTextIsSelectable(true)
        }
        termScroll = ScrollView(this).apply {
            background = rounded("#0D1117", 16f)
            setPadding(dp(12), dp(12), dp(12), dp(12))
            addView(termOut)
        }
        root.addView(termScroll, LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f))

        val row = inputRow("ls /sdcard", ::runTerminalCommand)
        termInput = lastInput
        // terminal wants a real shell prompt char
        (row.getChildAt(0) as TextView).text = "$ "
        root.addView(row)
        return root
    }

    private fun runTerminalCommand() {
        val cmd = termInput.text.toString()
        termInput.text.clear()
        termSession?.send(cmd) ?: termAppend("[shell not ready]\n")
    }

    private fun termAppend(s: String) {
        termOut.append(s)
        // cheap cap: keep last ~40k chars
        if (termOut.length() > 40000) {
            termOut.text = termOut.text.substring(termOut.length() - 30000)
        }
        termScroll.post { termScroll.fullScroll(ScrollView.FOCUS_DOWN) }
    }

    // ================= INSPECT TAB =================

    private fun buildInspectTab(): LinearLayout {
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(14), 0, dp(14), dp(14))
        }
        val btnRow = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            setPadding(0, 0, 0, dp(8))
        }
        fun addBtn(label: String, onClick: () -> Unit) {
            val b = Button(this).apply {
                text = label
                textSize = 11f
                isAllCaps = false
                setTextColor(Color.parseColor("#D0BCFF"))
                background = rounded("#1E1E28", 10f)
                stateListAnimator = null
                setPadding(dp(10), dp(8), dp(10), dp(8))
                setOnClickListener { onClick() }
            }
            b.layoutParams = LinearLayout.LayoutParams(0,
                LinearLayout.LayoutParams.WRAP_CONTENT, 1f).apply {
                if (btnRow.childCount > 0) leftMargin = dp(6)
            }
            btnRow.addView(b)
        }
        addBtn("TREE", ::dumpTree)
        addBtn("PICK", ::armPickHere)
        addBtn("MAIN", ::inspectMain)
        root.addView(btnRow)

        val btnRow2 = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            setPadding(0, 0, 0, dp(8))
        }
        fun addBtn2(label: String, onClick: () -> Unit) {
            val b = Button(this@ConsoleActivity).apply {
                text = label
                textSize = 11f
                isAllCaps = false
                setTextColor(Color.parseColor("#9A9AA5"))
                background = rounded("#1A1A20", 10f)
                stateListAnimator = null
                setPadding(dp(10), dp(8), dp(10), dp(8))
                setOnClickListener { onClick() }
            }
            b.layoutParams = LinearLayout.LayoutParams(0,
                LinearLayout.LayoutParams.WRAP_CONTENT, 1f).apply {
                if (btnRow2.childCount > 0) leftMargin = dp(6)
            }
            btnRow2.addView(b)
        }
        addBtn2("APP", { insPrint(ViewInspector.appInfo(this)) })
        addBtn2("PERMS", { insPrint(ViewInspector.permissionInfo(this)) })
        addBtn2("LOGCAT", {
            insPrint("reading logcat…")
            Thread { val t = ViewInspector.logcatTail(); ui.post { insPrint(t) } }.start()
        })
        addBtn2("CLEAR", { insOut.text = "" })
        root.addView(btnRow2)

        insOut = TextView(this).apply {
            typeface = Typeface.MONOSPACE
            textSize = 11.5f
            setTextColor(Color.parseColor("#9ECFFF"))
            setLineSpacing(dp(2).toFloat(), 1f)
            setTextIsSelectable(true)
        }
        insScroll = ScrollView(this).apply {
            background = rounded("#0D1420", 16f)
            setPadding(dp(12), dp(12), dp(12), dp(12))
            addView(insOut)
        }
        root.addView(insScroll, LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f))
        return root
    }

    private fun dumpTree() {
        val content = findViewById<View>(android.R.id.content)
        val tree = ViewInspector.dumpTree(content ?: window.decorView)
        insPrint("── view tree ──\n$tree")
    }

    private fun armPickHere() {
        ViewInspector.armPickMode(this,
            onPick = { v ->
                insPrint("── picked ──\n${ViewInspector.describe(v)}")
                AlertDialog.Builder(this, android.R.style.Theme_Material_Dialog)
                    .setTitle(ViewInspector.nodeLabel(v))
                    .setMessage(ViewInspector.describe(v))
                    .setPositiveButton("OK", null)
                    .show()
            },
            onDone = { insPrint("pick mode off.") })
        insPrint("pick mode armed — tap any element, DONE to exit.")
    }

    private fun inspectMain() {
        InspectorState.armed = true
        InspectorState.target = MainActivity::class.java
        startActivity(Intent(this, MainActivity::class.java))
    }

    private fun insPrint(msg: String) {
        insOut.append("[${timeFmt.format(Date())}] $msg\n")
        if (insOut.length() > 60000) {
            insOut.text = insOut.text.substring(insOut.length() - 45000)
        }
        insScroll.post { insScroll.fullScroll(ScrollView.FOCUS_DOWN) }
    }

    // ================= helpers =================

    private fun dp(v: Int): Int = (v * resources.displayMetrics.density).toInt()

    private fun rounded(colorHex: String, r: Float): GradientDrawable =
        GradientDrawable().apply {
            shape = GradientDrawable.RECTANGLE
            setColor(Color.parseColor(colorHex))
            cornerRadius = r * resources.displayMetrics.density
        }

    companion object {
        private val HELP = """
            |core:
            |  status            stream state + telemetry
            |  settings          dev settings
            |  start|stop|restart stream control
            |  clients           quest connections
            |  keyframe          force IDR keyframe
            |tuning (restart stream to apply):
            |  bitrate <bps>     500000–20000000
            |  res <w> <h>       320–1920 each
            |  fps <n>           15–60
            |  ip <addr>|clear   discovery IP override
            |  verbose <on|off>  logcat verbosity
            |network:
            |  ping <host>       icmp ping
            |  tcping <h> <port> tcp connect latency
            |  diag              full compatibility report
            |tabs:
            |  sh                jump to TERMINAL tab
            |  inspect|tree       jump to INSPECT tab
            |misc:
            |  tips              pro tips
            |  clear             clear screen
            |  whoami            version + device
            |hidden: godmode · matrix · sudo
        """.trimMargin()

        private val TIPS = """
            |pro tips:
            |• 5 GHz Wi-Fi, same AP as the Quest.
            |• 2 Mbps @ 720p/30 is the sweet spot.
            |• Dropped frames rising → lower bitrate first.
            |• Force a keyframe right after the Quest connects.
            |• TERMINAL tab runs a real shell as the app uid.
            |• INSPECT tab: TREE dumps views, PICK taps to inspect.
        """.trimMargin()

        private val MATRIX = """
            |Wake up, Neo…
            |The Matrix has you.
            |Follow the white rabbit. 🐇
        """.trimMargin()
    }
}
