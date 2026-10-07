package com.zachery.xrwrist.phone

import android.app.Activity
import android.app.ActivityManager
import android.content.Context
import android.content.pm.PackageManager
import android.graphics.Color
import android.graphics.drawable.GradientDrawable
import android.view.Gravity
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.FrameLayout
import android.widget.TextView

/**
 * Cross-activity inspect state — set before launching the target activity.
 */
object InspectorState {
    var armed: Boolean = false
    var target: Class<out Activity>? = null
}

/**
 * Browser-inspector-style view tools: tree dump, hit testing, property
 * readout, highlight, and a tap-to-pick overlay that lives inside our own
 * window (no special permissions required).
 */
object ViewInspector {

    // ---------- tree ----------

    fun dumpTree(root: View, maxDepth: Int = 12): String {
        val sb = StringBuilder()
        dumpNode(root, "", true, 0, maxDepth, sb)
        return sb.toString()
    }

    private fun dumpNode(v: View, prefix: String, last: Boolean, depth: Int,
                         maxDepth: Int, sb: StringBuilder) {
        if (depth > maxDepth) { sb.append(prefix).append("… (truncated)\n"); return }
        sb.append(prefix).append(if (last) "└─ " else "├─ ")
            .append(nodeLabel(v)).append('\n')
        if (v is ViewGroup) {
            val n = v.childCount
            for (i in 0 until n) {
                dumpNode(v.getChildAt(i), prefix + if (last) "   " else "│  ",
                    i == n - 1, depth + 1, maxDepth, sb)
            }
        }
    }

    fun nodeLabel(v: View): String {
        val cls = v.javaClass.simpleName
        val id = idName(v)
        val vis = when (v.visibility) {
            View.VISIBLE -> "V"; View.INVISIBLE -> "I"; else -> "G" }
        val loc = IntArray(2).also { v.getLocationOnScreen(it) }
        return "$cls#$id [$vis] (${loc[0]},${loc[1]} ${v.width}×${v.height})"
    }

    private fun idName(v: View): String = try {
        if (v.id != View.NO_ID) v.resources.getResourceEntryName(v.id) else "no-id"
    } catch (_: Exception) { "no-id" }

    // ---------- hit testing ----------

    /** Deepest topmost VISIBLE view under (x, y) in screen coords. */
    fun findAt(root: View, x: Float, y: Float): View? {
        if (!contains(root, x, y)) return null
        if (root is ViewGroup) {
            for (i in root.childCount - 1 downTo 0) {
                findAt(root.getChildAt(i), x, y)?.let { return it }
            }
        }
        return root
    }

    private fun contains(v: View, x: Float, y: Float): Boolean {
        if (v.visibility != View.VISIBLE || v.width <= 0 || v.height <= 0) return false
        val loc = IntArray(2).also { v.getLocationOnScreen(it) }
        return x >= loc[0] && x <= loc[0] + v.width &&
               y >= loc[1] && y <= loc[1] + v.height
    }

    // ---------- properties ----------

    fun describe(v: View): String {
        val sb = StringBuilder()
        sb.append("class: ").append(v.javaClass.name).append('\n')
        sb.append("id: ").append(idName(v)).append('\n')
        (v as? TextView)?.let { sb.append("text: \"")
            .append(it.text?.toString()?.take(160)).append("\"\n") }
        sb.append("contentDesc: ").append(v.contentDescription ?: "—").append('\n')
        sb.append("visibility: ").append(
            when (v.visibility) {
                View.VISIBLE -> "VISIBLE"
                View.INVISIBLE -> "INVISIBLE"; else -> "GONE" }).append('\n')
        sb.append("enabled=").append(v.isEnabled)
            .append(" clickable=").append(v.isClickable)
            .append(" longClickable=").append(v.isLongClickable)
            .append(" focusable=").append(v.isFocusable).append('\n')
        val loc = IntArray(2).also { v.getLocationOnScreen(it) }
        sb.append("screen=(${loc[0]},${loc[1]}) size=${v.width}×${v.height}\n")
        sb.append("measured=${v.measuredWidth}×${v.measuredHeight}\n")
        sb.append("padding=${v.paddingLeft},${v.paddingTop}," +
            "${v.paddingRight},${v.paddingBottom}\n")
        sb.append("alpha=${v.alpha} elevation=${v.elevation} rotation=${v.rotation}")
        return sb.toString()
    }

    // ---------- highlight ----------

    private var highlighted: View? = null
    private var prevFg: android.graphics.drawable.Drawable? = null

    fun highlight(v: View) {
        clearHighlight()
        highlighted = v
        prevFg = v.foreground
        v.foreground = GradientDrawable().apply {
            shape = GradientDrawable.RECTANGLE
            setStroke(6, Color.parseColor("#FF4081"))
            setColor(Color.parseColor("#18FF4081"))
        }
        v.invalidate()
    }

    fun clearHighlight() {
        highlighted?.let { it.foreground = prevFg; it.invalidate() }
        highlighted = null; prevFg = null
    }

    // ---------- tap-to-pick overlay ----------

    /**
     * Adds a touch-transparent overlay inside [activity]'s own window.
     * Taps hit-test the activity content (android.R.id.content); the picked
     * view is highlighted and delivered to [onPick]. A floating DONE button
     * ends the session via [onDone].
     */
    fun armPickMode(activity: Activity, onPick: (View) -> Unit, onDone: () -> Unit) {
        val decor = activity.window.decorView as ViewGroup
        val content = decor.findViewById<View>(android.R.id.content)

        val overlay = FrameLayout(activity).apply {
            setBackgroundColor(Color.parseColor("#0DFF4081"))
        }
        val hint = TextView(activity).apply {
            text = "◉ PICK MODE — tap any element"
            setTextColor(Color.WHITE)
            textSize = 13f
            setBackgroundColor(Color.parseColor("#CCFF4081"))
            setPadding(24, 16, 24, 16)
        }
        val doneBtn = Button(activity).apply {
            text = "DONE"
            setOnClickListener {
                clearHighlight()
                (overlay.parent as? ViewGroup)?.removeView(overlay)
                onDone()
            }
        }
        val topRow = android.widget.LinearLayout(activity).apply {
            orientation = android.widget.LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setPadding(16, 16, 16, 16)
            addView(hint, android.widget.LinearLayout.LayoutParams(
                0, android.widget.LinearLayout.LayoutParams.WRAP_CONTENT, 1f))
            addView(doneBtn)
        }
        overlay.addView(topRow, FrameLayout.LayoutParams(
            FrameLayout.LayoutParams.MATCH_PARENT,
            FrameLayout.LayoutParams.WRAP_CONTENT))

        overlay.setOnTouchListener { _, ev ->
            if (ev.action == MotionEvent.ACTION_DOWN) {
                // Don't pick the overlay chrome itself.
                val hit = content?.let { findAt(it, ev.rawX, ev.rawY) }
                if (hit != null && hit != content) {
                    highlight(hit)
                    onPick(hit)
                }
            }
            true
        }
        decor.addView(overlay, ViewGroup.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.MATCH_PARENT))
    }

    // ---------- app info ----------

    fun appInfo(ctx: Context): String {
        val sb = StringBuilder()
        val pm = ctx.packageManager
        val pkg = ctx.packageName
        try {
            val pi = pm.getPackageInfo(pkg, 0)
            @Suppress("DEPRECATION")
            sb.append("package: $pkg\n")
            sb.append("version: ${pi.versionName} (${pi.versionCode})\n")
            sb.append("firstInstall: ${java.text.SimpleDateFormat("yyyy-MM-dd HH:mm",
                java.util.Locale.US).format(java.util.Date(pi.firstInstallTime))}\n")
            sb.append("targetSdk=${pi.applicationInfo?.targetSdkVersion} " +
                "minSdk=${pi.applicationInfo?.minSdkVersion}\n\n")
            val withActs = pm.getPackageInfo(pkg, PackageManager.GET_ACTIVITIES)
            sb.append("activities (${withActs.activities?.size ?: 0}):\n")
            withActs.activities?.forEach {
                sb.append("  • ${it.name.substringAfterLast('.')} " +
                    "exported=${it.exported}\n")
            }
            val withSvcs = pm.getPackageInfo(pkg, PackageManager.GET_SERVICES)
            sb.append("services (${withSvcs.services?.size ?: 0}):\n")
            withSvcs.services?.forEach {
                sb.append("  • ${it.name.substringAfterLast('.')}\n")
            }
            val am = ctx.getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager
            @Suppress("DEPRECATION")
            val running = am.getRunningServices(20)
                .filter { it.service.packageName == pkg }
            sb.append("running now (${running.size}):\n")
            running.forEach {
                sb.append("  • ${it.service.shortClassName} " +
                    "fg=${it.foreground}\n")
            }
        } catch (e: Exception) {
            sb.append("error: ${e.message}\n")
        }
        return sb.toString()
    }

    fun permissionInfo(ctx: Context): String {
        val sb = StringBuilder()
        try {
            val pi = ctx.packageManager.getPackageInfo(
                ctx.packageName, PackageManager.GET_PERMISSIONS)
            val perms = pi.requestedPermissions ?: emptyArray()
            val flags = pi.requestedPermissionsFlags ?: IntArray(0)
            sb.append("requested (${perms.size}):\n")
            perms.forEachIndexed { i, p ->
                val granted = i < flags.size &&
                    (flags[i] and PackageManager.REQUESTED_PERMISSION_GRANTED) != 0
                sb.append("  ${if (granted) "✓" else "✗"} ${p.substringAfterLast('.')}\n")
            }
        } catch (e: Exception) {
            sb.append("error: ${e.message}\n")
        }
        return sb.toString()
    }

    /** Last 200 logcat lines for our own logs (all tags need READ_LOGS grant). */
    fun logcatTail(): String = try {
        val p = Runtime.getRuntime().exec(arrayOf("logcat", "-d", "-t", "200", "*:D"))
        val out = p.inputStream.bufferedReader().readText()
        p.waitFor()
        if (out.isBlank()) "(empty — grant READ_LOGS via adb for system-wide logs)"
        else out
    } catch (e: Exception) {
        "logcat failed: ${e.message}"
    }
}
