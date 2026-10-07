package com.zachery.xrwrist.phone

import android.animation.ValueAnimator
import android.app.Activity
import android.content.Intent
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.graphics.drawable.RippleDrawable
import android.content.res.ColorStateList
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.text.InputType
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.view.animation.DecelerateInterpolator
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.Switch
import android.widget.TextView
import android.widget.Toast
import java.io.File

/**
 * Developer Options — Material 3 dark expressive UI.
 *
 * Cards: logging, network, video pipeline, telemetry, compatibility,
 * admin operations, console entry, tips. Hidden GODMODE section
 * (triple-tap the header) adds network diagnostics, bitrate graph,
 * encoder power tricks and secret console commands.
 */
class DevOptionsActivity : Activity() {

    // ---- M3 dark baseline palette ----
    private object M3 {
        const val PRIMARY = "#D0BCFF"
        const val ON_PRIMARY = "#381E72"
        const val PRIMARY_CONTAINER = "#4F378B"
        const val ON_PRIMARY_CONTAINER = "#EADDFF"
        const val SURFACE = "#141218"
        const val ON_SURFACE = "#E6E0E9"
        const val SURFACE_CONTAINER = "#1D1B20"
        const val SURFACE_CONTAINER_HIGH = "#2B2930"
        const val OUTLINE = "#938F99"
        const val OUTLINE_VARIANT = "#49454F"
        const val ERROR = "#F2B8B8"
        const val ON_ERROR_CONTAINER = "#8C1D18"
        const val ERROR_CONTAINER = "#F2B8B8"
        const val TERTIARY = "#EFB8C8"
        const val SUCCESS = "#7DD8A8"
    }

    private lateinit var statsText: TextView
    private lateinit var godmodeSection: LinearLayout
    private lateinit var diagOutput: TextView
    private lateinit var graphView: BitrateGraphView
    private lateinit var questIpInput: EditText
    private val uiHandler = Handler(Looper.getMainLooper())

    private val statsUpdater = object : Runnable {
        override fun run() {
            if (isFinishing) return
            statsText.text = buildStatsText()
            if (::graphView.isInitialized && godmodeVisible()) graphView.refresh()
            uiHandler.postDelayed(this, 1000)
        }
    }

    // Triple-tap detection for the hidden Godmode unlock.
    private var tapTimes = mutableListOf<Long>()
    private fun onHeaderTap() {
        val now = System.currentTimeMillis()
        tapTimes.add(now)
        tapTimes = tapTimes.filter { now - it < 1500 }.toMutableList()
        if (tapTimes.size >= 3) {
            tapTimes.clear()
            if (!DevSettings.godmodeUnlocked) {
                DevSettings.godmodeUnlocked = true
                toast("GODMODE UNLOCKED")
                revealGodmode()
            } else {
                toast("Godmode already unlocked")
            }
        }
    }

    private fun godmodeVisible() =
        ::godmodeSection.isInitialized && godmodeSection.visibility == View.VISIBLE

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        DevSettings.init(this)
        Compat.ensureNotificationPermission(this)

        val root = ScrollView(this).apply {
            setBackgroundColor(Color.parseColor(M3.SURFACE))
        }
        val layout = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(16), dp(16), dp(16), dp(48))
        }

        // ---- Header (triple-tap = godmode) ----
        val header = TextView(this).apply {
            text = "Developer Options"
            textSize = 24f
            typeface = Typeface.create("sans-serif-medium", Typeface.NORMAL)
            setTextColor(Color.parseColor(M3.ON_SURFACE))
            setPadding(0, dp(8), 0, dp(2))
            isClickable = true
            isFocusable = true
            setOnClickListener { onHeaderTap() }
        }
        layout.addView(header)
        layout.addView(TextView(this).apply {
            text = "Tune the pipeline, watch it live, run admin ops."
            textSize = 13f
            setTextColor(Color.parseColor(M3.OUTLINE))
            setPadding(0, 0, 0, dp(12))
        })

        val cards = mutableListOf<View>()
        cards += loggingCard()
        cards += networkCard()
        cards += videoCard()
        cards += telemetryCard()
        cards += compatibilityCard()
        cards += adminCard()
        cards += consoleCard()
        godmodeSection = godmodeCard()
        godmodeSection.visibility =
            if (DevSettings.godmodeUnlocked) View.VISIBLE else View.GONE
        cards += godmodeSection

        cards.forEach { layout.addView(it) }
        root.addView(layout)
        setContentView(root)

        // Staggered entrance: fade + rise.
        cards.forEachIndexed { i, v ->
            v.alpha = 0f
            v.translationY = dp(24).toFloat()
            v.animate().alpha(1f).translationY(0f)
                .setStartDelay((i * 60).toLong())
                .setDuration(320)
                .setInterpolator(DecelerateInterpolator())
                .start()
        }
    }

    override fun onResume() {
        super.onResume()
        uiHandler.post(statsUpdater)
    }

    override fun onPause() {
        super.onPause()
        uiHandler.removeCallbacks(statsUpdater)
    }

    private fun revealGodmode() {
        godmodeSection.visibility = View.VISIBLE
        godmodeSection.alpha = 0f
        godmodeSection.animate().alpha(1f).setDuration(400).start()
    }

    // ================= CARDS =================

    private fun loggingCard(): View = m3Card("Logging",
        "Controls how chatty the app is in logcat. Verbose mode also " +
        "logs every discovery reply and video handshake — useful when the " +
        "Quest can't find your phone.") {
        val sw = Switch(this@DevOptionsActivity).apply {
            text = "Verbose logging"
            setTextColor(Color.parseColor(M3.ON_SURFACE))
            isChecked = DevSettings.verboseLogging
            setOnCheckedChangeListener { _, c ->
                DevSettings.verboseLogging = c
                toast("Verbose ${if (c) "ON" else "OFF"}")
            }
        }
        addView(sw)
    }

    private fun networkCard(): View = m3Card("Network",
        "The discovery reply tells the Quest where to connect. Override " +
        "the IP only if auto-detect picks the wrong interface (e.g. VPN " +
        "or Tailscale). The name shows up in Quest-side logs.") {
        val ip = labeledInput("IP override (blank = auto-detect)",
            DevSettings.ipOverride, InputType.TYPE_CLASS_TEXT)
        val name = labeledInput("Discovery name",
            DevSettings.discoveryName, InputType.TYPE_CLASS_TEXT)
        addView(tonalButton("Save network settings") {
            val v = ip.text.toString().trim()
            if (v.isNotEmpty() && !v.matches(Regex("""\d{1,3}(\.\d{1,3}){3}"""))) {
                toast("That doesn't look like an IPv4 address")
                return@tonalButton
            }
            DevSettings.ipOverride = v
            DevSettings.discoveryName = name.text.toString().ifBlank { "ziggy" }
            toast("Saved — takes effect immediately")
        })
    }

    private fun videoCard(): View = m3Card("Video pipeline",
        "Encoder settings. Higher bitrate = sharper image but more Wi-Fi " +
        "load. 720×1280 @ 30fps / 2 Mbps is the sweet spot for Quest 3. " +
        "Changes apply on the next stream start — use Restart below.") {
        val w = labeledInput("Width (320–1920)",
            DevSettings.videoWidth.toString(), InputType.TYPE_CLASS_NUMBER)
        val h = labeledInput("Height (320–1920)",
            DevSettings.videoHeight.toString(), InputType.TYPE_CLASS_NUMBER)
        val fps = labeledInput("FPS (15–60)",
            DevSettings.videoFps.toString(), InputType.TYPE_CLASS_NUMBER)
        val br = labeledInput("Bitrate, bps (500 000–20 000 000)",
            DevSettings.videoBitrate.toString(), InputType.TYPE_CLASS_NUMBER)
        addView(tonalButton("Save video settings") {
            try {
                DevSettings.videoWidth = w.text.toString().toInt()
                DevSettings.videoHeight = h.text.toString().toInt()
                DevSettings.videoFps = fps.text.toString().toInt()
                DevSettings.videoBitrate = br.text.toString().toInt()
                toast("Saved — restart the stream to apply")
            } catch (_: NumberFormatException) { toast("Numbers only, please") }
        })
        addView(outlineButton("Reset all to defaults") {
            DevSettings.resetToDefaults()
            recreate()
        })
    }

    private fun telemetryCard(): View {
        var card: LinearLayout? = null
        card = m3Card("Live telemetry",
            "Real-time pipeline state. State machine: IDLE → STARTING → " +
            "STREAMING → STOPPING → IDLE. ERROR means check Last error.") {
            statsText = TextView(this@DevOptionsActivity).apply {
                typeface = Typeface.MONOSPACE
                textSize = 12.5f
                setTextColor(Color.parseColor(M3.SUCCESS))
                setBackgroundColor(Color.parseColor("#0E0E12"))
                setPadding(dp(12), dp(12), dp(12), dp(12))
                background = roundedBg("#0E0E12", 16f, null)
            }
            addView(statsText)
        }
        return card!!
    }

    private fun compatibilityCard(): View = m3Card("Compatibility",
        "Checks this phone against the streaming requirements: OS version, " +
        "OEM quirks (Samsung / Xiaomi / Oppo battery killers), notification " +
        "permission, and a live H.264 encoder probe with your current settings.") {
        val out = TextView(this@DevOptionsActivity).apply {
            typeface = Typeface.MONOSPACE
            textSize = 12f
            setTextColor(Color.parseColor(M3.ON_SURFACE))
            setBackgroundColor(Color.parseColor("#0E0E12"))
            background = roundedBg("#0E0E12", 16f, null)
            setPadding(dp(12), dp(12), dp(12), dp(12))
            text = Compat.deviceLine() + "\n\nTap below to run the full check."
        }
        addView(out)
        addView(tonalButton("Run compatibility check") {
            out.text = "Running… (encoder probe takes a moment)"
            Thread {
                val report = Compat.runDiagnostics(this@DevOptionsActivity)
                uiHandler.post { out.text = report }
            }.start()
        })
        if (!Compat.isIgnoringBatteryOptimizations(this@DevOptionsActivity)) {
            addView(outlineButton("Allow background execution (recommended)") {
                Compat.requestIgnoreBatteryOptimizations(this@DevOptionsActivity)
            })
        }
    }

    private fun adminCard(): View = m3Card("Admin operations",
        "Restart reuses your existing screen-capture grant — no system " +
        "dialog. Re-request permission wipes the grant and takes you back " +
        "to the main screen for a fresh approval.") {
        addView(tonalButton("Restart streaming service") { restartService() })
        addView(outlineButton("Force keyframe (IDR)") {
            toast(if (StreamControl.keyFrame()) "Keyframe requested"
            else "No active encoder")
        })
        addView(outlineButton("Re-request screen capture permission") {
            ProjectionHolder.clear()
            startActivity(Intent(this@DevOptionsActivity, MainActivity::class.java).apply {
                putExtra("force_projection", true)
                addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP)
            })
            finish()
        })
        addView(outlineButton("Export logs to file") { exportLogs() })
        addView(dangerButton("Clear app cache") { clearCache() })
    }

    private fun consoleCard(): View = m3Card("Console",
        "A terminal into the app's internals: status, clients, bitrate, " +
        "resolution and more. Type 'help' once inside. " +
        "Psst — there are secret commands…") {
        addView(tonalButton("Open console") {
            startActivity(Intent(this@DevOptionsActivity, ConsoleActivity::class.java))
        })
    }

    // ================= GODMODE =================

    private fun godmodeCard(): LinearLayout {
        val section = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
        }
        section.addView(TextView(this).apply {
            text = "⚡ GODMODE"
            textSize = 18f
            typeface = Typeface.create("sans-serif-medium", Typeface.NORMAL)
            setTextColor(Color.parseColor(M3.TERTIARY))
            setPadding(0, dp(20), 0, dp(4))
        })

        // --- Network diagnostics ---
        section.addView(m3Card("Network diagnostics",
            "Ping the Quest, measure TCP handshake latency on the video / " +
            "control ports, or run a TTL traceroute. Target defaults to " +
            "the last connected video client.") {
            questIpInput = labeledInput("Quest IP",
                StreamStats.lastVideoClientIp.ifBlank { DevSettings.ipOverride },
                InputType.TYPE_CLASS_TEXT)
            addView(tonalButton("Ping") { runDiag("ping") })
            addView(outlineButton("Port scan (8899/8900/8901)") { runDiag("scan") })
            addView(outlineButton("TCP latency") { runDiag("tcp") })
            addView(outlineButton("Traceroute") { runDiag("trace") })
            diagOutput = TextView(this@DevOptionsActivity).apply {
                typeface = Typeface.MONOSPACE
                textSize = 12f
                setTextColor(Color.parseColor(M3.TERTIARY))
                setBackgroundColor(Color.parseColor("#0E0E12"))
                background = roundedBg("#0E0E12", 16f, null)
                setPadding(dp(12), dp(12), dp(12), dp(12))
                text = "Results appear here."
            }
            addView(diagOutput)
        })

        // --- Encoder lab ---
        section.addView(m3Card("Encoder lab",
            "Live bitrate graph (60 s window), dropped-frame counter and " +
            "one-tap IDR keyframe. Dropped frames usually mean the SoC " +
            "can't keep up — lower bitrate or resolution.") {
            graphView = BitrateGraphView(this@DevOptionsActivity).apply {
                layoutParams = LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT, dp(180)).apply {
                    setMargins(0, dp(4), 0, dp(8))
                }
                setBackgroundColor(Color.parseColor("#0E0E12"))
                background = roundedBg("#0E0E12", 16f, null)
            }
            addView(graphView)
            addView(tonalButton("Force keyframe now") {
                toast(if (StreamControl.keyFrame()) "IDR requested" else "No active encoder")
            })
        })

        // --- Secrets & pro tips ---
        section.addView(m3Card("Secrets & pro tips",
            "Unlocked extras. The console hides a few easter eggs — " +
            "try 'matrix'. These tips are the distilled field notes from " +
            "getting this stream stable.") {
            val tips = TextView(this@DevOptionsActivity).apply {
                textSize = 13f
                setTextColor(Color.parseColor(M3.ON_SURFACE))
                setLineSpacing(dp(4).toFloat(), 1f)
                text = PRO_TIPS
            }
            addView(tips)
        })
        return section
    }

    private fun runDiag(kind: String) {
        val host = questIpInput.text.toString().trim()
        if (host.isEmpty()) { toast("Enter the Quest IP first"); return }
        diagOutput.text = "Running $kind → $host …"
        Thread {
            val res = when (kind) {
                "ping" -> NetDiag.ping(host).let {
                    "ping ${it.host}: ${if (it.ok) "OK" else "FAILED"}\n" +
                    "avg ${if (it.avgMs >= 0) "${"%.1f".format(it.avgMs)} ms" else "n/a"} — " +
                    "${it.received}/${it.transmitted} received\n${it.raw.take(600)}"
                }
                "scan" -> NetDiag.questPortScan(host)
                "tcp" -> NetDiag.tcpLatency(host, NetworkServer.PORT_VIDEO) + "\n" +
                    NetDiag.tcpLatency(host, NetworkServer.PORT_CONTROL)
                "trace" -> NetDiag.traceroute(host)
                else -> "?"
            }
            uiHandler.post { diagOutput.text = res }
        }.start()
    }

    // ================= helpers =================

    private fun buildStatsText(): String = buildString {
        appendLine("── STREAM STATE ──")
        append(StreamStats.summary())
        appendLine()
        appendLine("── SETTINGS ──")
        append(DevSettings.summary())
    }

    private fun restartService() {
        if (!ProjectionHolder.hasGrant) {
            toast("No capture grant stored — start streaming from the main screen first")
            return
        }
        startForegroundService(Intent(this, StreamService::class.java).apply {
            action = StreamService.ACTION_RESTART
        })
        toast("Restarting stream…")
    }

    private fun exportLogs() {
        try {
            val proc = Runtime.getRuntime().exec(arrayOf(
                "logcat", "-d", "-v", "threadtime",
                "XRWristStream:D", "XRWristNet:D", "XRWristEncoder:D",
                "XRWristTouch:D", "XRWristCompat:D", "*:S"))
            val output = proc.inputStream.bufferedReader().readText()
            proc.waitFor()
            val dir = getExternalFilesDir(null) ?: cacheDir
            val file = File(dir, "xrwrist-logs-${System.currentTimeMillis()}.txt")
            file.writeText(output.ifBlank { "(no log output captured)" })
            toast("Exported: ${file.name}")
        } catch (e: Exception) {
            toast("Export failed: ${e.message}")
        }
    }

    private fun clearCache() {
        try {
            var n = 0
            cacheDir.walkTopDown().forEach { if (it.isFile && it.delete()) n++ }
            toast("Cache cleared ($n files)")
        } catch (e: Exception) {
            toast("Clear failed: ${e.message}")
        }
    }

    // ---------- M3 component builders ----------

    private fun dp(v: Int): Int = (v * resources.displayMetrics.density).toInt()

    private fun roundedBg(colorHex: String, radiusDp: Float, strokeHex: String?): GradientDrawable =
        GradientDrawable().apply {
            shape = GradientDrawable.RECTANGLE
            setColor(Color.parseColor(colorHex))
            cornerRadius = radiusDp * resources.displayMetrics.density
            strokeHex?.let { setStroke(dp(1), Color.parseColor(it)) }
        }

    /** Outlined M3 card with an info-tip toggle. */
    private fun m3Card(title: String, tip: String, body: LinearLayout.() -> Unit): LinearLayout {
        val card = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            background = roundedBg(M3.SURFACE_CONTAINER, 20f, M3.OUTLINE_VARIANT)
            setPadding(dp(16), dp(12), dp(16), dp(16))
            val lp = LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT)
            lp.setMargins(0, dp(6), 0, dp(6))
            layoutParams = lp
        }
        val headerRow = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }
        headerRow.addView(TextView(this).apply {
            text = title
            textSize = 16f
            typeface = Typeface.create("sans-serif-medium", Typeface.NORMAL)
            setTextColor(Color.parseColor(M3.ON_SURFACE))
            layoutParams = LinearLayout.LayoutParams(0,
                LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
        })
        val tipBody = TextView(this).apply {
            text = tip
            textSize = 12.5f
            setTextColor(Color.parseColor(M3.OUTLINE))
            setLineSpacing(dp(3).toFloat(), 1f)
            visibility = View.GONE
            setPadding(0, dp(6), 0, dp(8))
        }
        val infoBtn = TextView(this).apply {
            text = "ⓘ"
            textSize = 18f
            setTextColor(Color.parseColor(M3.PRIMARY))
            setPadding(dp(8), dp(4), dp(4), dp(4))
            isClickable = true
            setOnClickListener { toggleExpand(tipBody) }
        }
        headerRow.addView(infoBtn)
        card.addView(headerRow)
        card.addView(tipBody)
        card.body()
        return card
    }

    private fun LinearLayout.labeledInput(label: String, value: String, inputType: Int): EditText {
        addView(TextView(context).apply {
            text = label
            textSize = 12f
            setTextColor(Color.parseColor(M3.OUTLINE))
            setPadding(0, dp(10), 0, dp(4))
        })
        return EditText(context).apply {
            setText(value)
            this.inputType = inputType
            setTextColor(Color.parseColor(M3.ON_SURFACE))
            setHintTextColor(Color.parseColor(M3.OUTLINE_VARIANT))
            background = roundedBg(M3.SURFACE_CONTAINER_HIGH, 12f, M3.OUTLINE_VARIANT)
            setPadding(dp(14), dp(12), dp(14), dp(12))
            addView(this)
        }
    }

    private fun rippleBg(colorHex: String, radiusDp: Float): RippleDrawable =
        RippleDrawable(
            ColorStateList.valueOf(Color.parseColor("#40D0BCFF")),
            roundedBg(colorHex, radiusDp, null), null)

    private fun baseButton(text: String, bgHex: String, fgHex: String,
                           onClick: () -> Unit): Button =
        Button(this).apply {
            this.text = text
            textSize = 14f
            typeface = Typeface.create("sans-serif-medium", Typeface.NORMAL)
            setTextColor(Color.parseColor(fgHex))
            background = rippleBg(bgHex, 20f)
            stateListAnimator = null
            setPadding(0, dp(14), 0, dp(14))
            val lp = LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT)
            lp.setMargins(0, dp(6), 0, dp(6))
            layoutParams = lp
            isAllCaps = false
            setOnClickListener { onClick() }
        }

    /** Filled tonal button (M3). */
    private fun LinearLayout.tonalButton(text: String, onClick: () -> Unit): Button =
        baseButton(text, M3.PRIMARY_CONTAINER, M3.ON_PRIMARY_CONTAINER, onClick)
            .also { addView(it) }

    /** Outlined button (M3). */
    private fun LinearLayout.outlineButton(text: String, onClick: () -> Unit): Button {
        val b = baseButton(text, M3.SURFACE_CONTAINER, M3.PRIMARY, onClick)
        b.background = RippleDrawable(
            ColorStateList.valueOf(Color.parseColor("#40D0BCFF")),
            roundedBg(M3.SURFACE_CONTAINER, 20f, M3.OUTLINE), null)
        addView(b)
        return b
    }

    /** Destructive button. */
    private fun LinearLayout.dangerButton(text: String, onClick: () -> Unit): Button =
        baseButton(text, "#5C1512", M3.ERROR_CONTAINER, onClick).also { addView(it) }

    /** Animated expand/collapse for tip bodies. */
    private fun toggleExpand(v: View) {
        val expand = v.visibility != View.VISIBLE
        if (expand) {
            v.measure(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT)
            val target = v.measuredHeight
            v.layoutParams.height = 0
            v.visibility = View.VISIBLE
            ValueAnimator.ofInt(0, target).apply {
                duration = 260
                interpolator = DecelerateInterpolator()
                addUpdateListener {
                    v.layoutParams.height = it.animatedValue as Int
                    v.requestLayout()
                }
            }.start()
        } else {
            val start = v.height
            ValueAnimator.ofInt(start, 0).apply {
                duration = 220
                interpolator = DecelerateInterpolator()
                addUpdateListener {
                    v.layoutParams.height = it.animatedValue as Int
                    v.requestLayout()
                }
                doOnEnd { v.visibility = View.GONE }
            }.start()
        }
    }

    private fun ValueAnimator.doOnEnd(action: () -> Unit) {
        addListener(object : android.animation.AnimatorListenerAdapter() {
            override fun onAnimationEnd(a: android.animation.Animator) = action()
        })
    }

    private fun toast(msg: String) =
        Toast.makeText(this, msg, Toast.LENGTH_SHORT).show()

    companion object {
        private val PRO_TIPS = """
            |• 5 GHz Wi-Fi only — 2.4 GHz adds 100ms+ of jitter.
            |• Phone and Quest on the SAME access point; mesh hops hurt.
            |• 2 Mbps @ 720p is the sweet spot. 4 Mbps if your router is beefy.
            |• Dropped frames climbing? Lower bitrate before resolution.
            |• Force an IDR keyframe right after the Quest connects — it clears
            |  the "frozen first frame" glitch.
            |• 'adb shell dumpsys media_projection' shows the live session.
            |• Keep the phone plugged in: sustained encode throttles on battery
            |  saver on some SoCs.
            |• If discovery fails, hardcode the IP override — mDNS/UDP
            |  broadcast is the first thing guest networks block.
        """.trimMargin()
    }
}
