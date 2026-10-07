package com.zachery.xrwrist.phone

import android.app.Activity
import android.app.AlertDialog
import android.content.Context
import android.content.Intent
import android.media.projection.MediaProjectionManager
import android.os.Bundle
import android.provider.Settings
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast

class MainActivity : Activity() {

    companion object {
        private const val REQ_MEDIA_PROJECTION = 1001
    }

    private lateinit var statusText: TextView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        DevSettings.init(this)

        val layout = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(48, 48, 48, 48)
        }

        val title = TextView(this).apply {
            text = "XR Wrist Display — Phone Streamer"
            textSize = 20f
        }
        statusText = TextView(this).apply {
            text = "Status: idle"
            textSize = 16f
        }

        val startBtn = Button(this).apply {
            text = "Start Streaming"
            setOnClickListener { requestProjection() }
        }
        val stopBtn = Button(this).apply {
            text = "Stop"
            setOnClickListener { stopStreaming() }
        }
        val a11yBtn = Button(this).apply {
            text = "Enable Touch Service"
            setOnClickListener {
                startActivity(Intent(Settings.ACTION_ACCESSIBILITY_SETTINGS))
                Toast.makeText(this@MainActivity,
                    "Turn on 'XR Wrist Touch' for VR touch control",
                    Toast.LENGTH_LONG).show()
            }
        }
        val devBtn = Button(this).apply {
            text = "Developer Options"
            setOnClickListener {
                startActivity(Intent(this@MainActivity, DevOptionsActivity::class.java))
            }
        }

        layout.addView(title)
        layout.addView(statusText)
        layout.addView(startBtn)
        layout.addView(stopBtn)
        layout.addView(a11yBtn)
        layout.addView(devBtn)
        setContentView(layout)

        updateStatus()

        // Launched from Dev Options to force a fresh capture permission grant.
        if (intent?.getBooleanExtra("force_projection", false) == true) {
            intent.removeExtra("force_projection")
            requestProjection()
        }
    }

    private fun requestProjection() {
        // API 33+: notification permission keeps the foreground-service
        // notification visible while streaming.
        Compat.ensureNotificationPermission(this)
        val mpm = getSystemService(Context.MEDIA_PROJECTION_SERVICE) as MediaProjectionManager
        startActivityForResult(mpm.createScreenCaptureIntent(), REQ_MEDIA_PROJECTION)
    }

    private fun stopStreaming() {
        stopService(Intent(this, StreamService::class.java))
        updateStatus()
    }

    private fun updateStatus() {
        statusText.text = if (StreamService.isRunning) "Status: STREAMING" else "Status: idle"
    }

    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode == REQ_MEDIA_PROJECTION) {
            if (resultCode == RESULT_OK && data != null) {
                val svc = Intent(this, StreamService::class.java).apply {
                    putExtra(StreamService.EXTRA_RESULT_CODE, resultCode)
                    putExtra(StreamService.EXTRA_DATA, data)
                }
                startForegroundService(svc)
                statusText.text = "Status: STREAMING"
            } else {
                Toast.makeText(this, "Screen capture denied", Toast.LENGTH_SHORT).show()
            }
        }
    }

    override fun onResume() {
        super.onResume()
        updateStatus()
        if (InspectorState.armed && InspectorState.target == MainActivity::class.java) {
            InspectorState.armed = false
            InspectorState.target = null
            ViewInspector.armPickMode(this,
                onPick = { v ->
                    AlertDialog.Builder(this, android.R.style.Theme_Material_Dialog)
                        .setTitle(ViewInspector.nodeLabel(v))
                        .setMessage(ViewInspector.describe(v))
                        .setPositiveButton("OK", null)
                        .show()
                },
                onDone = { })
        }
    }
}
