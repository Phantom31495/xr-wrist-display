package com.zachery.xrwrist.phone

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.Path
import android.graphics.Shader
import android.util.AttributeSet
import android.view.View
import kotlin.math.max

/**
 * Scrolling bitrate graph for the Godmode section.
 * Renders [StreamStats] bitrate history as a smooth filled line
 * with grid lines and a live value readout. M3 dark styling.
 */
class BitrateGraphView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : View(context, attrs) {

    private val gridPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.parseColor("#2A2A2E")
        strokeWidth = 1f
    }
    private val linePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.parseColor("#D0BCFF")
        strokeWidth = 4f
        style = Paint.Style.STROKE
        strokeJoin = Paint.Join.ROUND
        strokeCap = Paint.Cap.ROUND
    }
    private val fillPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.FILL
    }
    private val textPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.parseColor("#E6E0E9")
        textSize = 30f
    }
    private val labelPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.parseColor("#938F99")
        textSize = 24f
    }

    private val path = Path()
    private val fillPath = Path()

    /** Refresh from the latest stats. Call on the UI thread (e.g. 1 Hz). */
    fun refresh() {
        invalidate()
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val w = width.toFloat()
        val h = height.toFloat()
        if (w <= 0 || h <= 0) return

        val samples = StreamStats.bitrateHistorySnapshot()
        val padL = 12f; val padR = 12f
        val padT = 56f; val padB = 36f
        val gw = w - padL - padR
        val gh = h - padT - padB

        // Grid: 4 horizontal lines.
        for (i in 0..4) {
            val y = padT + gh * i / 4f
            canvas.drawLine(padL, y, w - padR, y, gridPaint)
        }

        val peak = max(1f, samples.maxOrNull() ?: 1f) * 1.15f

        if (samples.size >= 2) {
            path.reset()
            fillPath.reset()
            samples.forEachIndexed { i, v ->
                val x = padL + gw * i / (samples.size - 1).coerceAtLeast(1)
                val y = padT + gh * (1f - (v / peak).coerceIn(0f, 1f))
                if (i == 0) {
                    path.moveTo(x, y)
                    fillPath.moveTo(x, padT + gh)
                    fillPath.lineTo(x, y)
                } else {
                    // Smooth curve via quadratic midpoint interpolation.
                    val prevX = padL + gw * (i - 1) / (samples.size - 1).coerceAtLeast(1)
                    val prevV = samples[i - 1]
                    val prevY = padT + gh * (1f - (prevV / peak).coerceIn(0f, 1f))
                    val midX = (prevX + x) / 2f
                    path.quadTo(prevX, prevY, midX, (prevY + y) / 2f)
                    fillPath.quadTo(prevX, prevY, midX, (prevY + y) / 2f)
                    if (i == samples.size - 1) path.lineTo(x, y)
                }
            }
            fillPath.lineTo(padL + gw, padT + gh)
            fillPath.close()

            fillPaint.shader = LinearGradient(
                0f, padT, 0f, padT + gh,
                Color.parseColor("#7DD0BCFF"), Color.parseColor("#0DD0BCFF"),
                Shader.TileMode.CLAMP)
            canvas.drawPath(fillPath, fillPaint)
            canvas.drawPath(path, linePaint)

            // Live dot at the head.
            val lastV = samples.last()
            val lastX = w - padR
            val lastY = padT + gh * (1f - (lastV / peak).coerceIn(0f, 1f))
            canvas.drawCircle(lastX, lastY, 8f, linePaint.apply { style = Paint.Style.FILL })
            linePaint.style = Paint.Style.STROKE
        } else {
            labelPaint.textAlign = Paint.Align.CENTER
            canvas.drawText("waiting for samples…", w / 2f, h / 2f, labelPaint)
            labelPaint.textAlign = Paint.Align.LEFT
        }

        // Readouts.
        val current = samples.lastOrNull() ?: 0f
        canvas.drawText("${"%.2f".format(current)} Mbps", padL + 8f, 40f, textPaint)
        canvas.drawText("peak ${"%.2f".format(peak / 1.15f)}", w - padR - 220f, 40f, labelPaint)
        canvas.drawText("60s window", padL + 8f, h - 8f, labelPaint)
    }
}
