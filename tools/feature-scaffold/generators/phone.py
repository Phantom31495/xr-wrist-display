"""Phone (Kotlin) scaffold generator for the XR Wrist Display project.

Conventions followed (from phone/app/src/main/java/com/zachery/xrwrist/phone/):
  - `package com.zachery.xrwrist.phone`
  - KDoc on every public type explaining threading/lifecycle
  - programmatic Views (no XML layouts), Material Design 3 dark styling
  - SharedPreferences-backed settings via the DevSettings pattern
"""

import re


def _class_name(name: str) -> str:
    name = re.sub(r"[^A-Za-z0-9]", "", name)
    return name[:1].upper() + name[1:] if name else "Feature"


COUNCIL_CHECKLIST_KT = """\
 * Council checklist — all seats must sign off before this ships:
 *   [ ] Engineering:    builds clean, no warnings, edge cases handled
 *   [ ] Human-Centered: feels natural to the human body / eyes / hands
 *   [ ] Security:       no credential/PII exposure, boundaries respected
 *   [ ] UX:             polished, professional, Material Design 3
 *   [ ] Platform:       minSdk 29 .. current, Samsung/Pixel quirks handled
 *   [ ] Researcher:     docs consulted, outcome simulated, provenance noted
 *   [ ] User Reviewer:  Zachery would actually use this (VETO if no)
"""

HUMAN_NOTE_KT = """\
 * Human-centered note: before adding behavior here, ask
 * "what does the human body want?" — then build to that.
"""


def _ui_panel(cls, desc):
    return f"""\
package com.zachery.xrwrist.phone

import android.app.Activity
import android.os.Bundle
import android.view.Gravity
import android.view.ViewGroup
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import android.graphics.Typeface

/**
 * {cls} — {desc}
 *
 * Material Design 3, programmatic Views, dark theme matching DevOptionsActivity.
 * Launched from the Dev Options screen; never from the launcher directly.
{COUNCIL_CHECKLIST_KT}{HUMAN_NOTE_KT} */
class {cls}Activity : Activity() {{

    override fun onCreate(savedInstanceState: Bundle?) {{
        super.onCreate(savedInstanceState)
        val root = LinearLayout(this).apply {{
            orientation = LinearLayout.VERTICAL
            setPadding(dp(16), dp(16), dp(16), dp(16))
            // TODO: dark background matching the app theme, e.g. 0xFF121212
        }}
        val scroll = ScrollView(this).apply {{ addView(root) }}

        root.addView(titleView("{cls}"))

        // TODO: build your controls here. Patterns from DevOptionsActivity:
        //   - section headers: small caps TextView
        //   - toggles: Switch with descriptive label
        //   - actions: tonal Button (danger actions get the danger style)
        //   - readouts: monospace TextView updated on a Handler loop

        setContentView(scroll)
    }}

    private fun titleView(text: String) = TextView(this).apply {{
        this.text = text
        textSize = 20f
        typeface = Typeface.DEFAULT_BOLD
    }}

    private fun dp(v: Int): Int = (v * resources.displayMetrics.density).toInt()
}}
"""


def _network_service(cls, desc):
    return f"""\
package com.zachery.xrwrist.phone

import android.app.Service
import android.content.Intent
import android.os.IBinder

/**
 * {cls} — {desc}
 *
 * Foreground service pattern (see StreamService): user-visible work needs a
 * persistent notification on Android 8+. All blocking I/O on a dedicated
 * thread — never the main thread.
{COUNCIL_CHECKLIST_KT}{HUMAN_NOTE_KT} */
class {cls}Service : Service() {{

    @Volatile private var running = false
    private var worker: Thread? = null

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {{
        if (running) return START_STICKY
        running = true
        // TODO: startForeground() with a notification channel BEFORE doing work
        // (required on API 26+ or the system kills the service).
        worker = Thread({{
            try {{
                // TODO: your network loop here. Use bounded timeouts on every
                // socket op — never block forever on a dead peer.
            }} finally {{
                running = false
            }}
        }}, "{cls}Worker").apply {{ isDaemon = true; start() }}
        return START_STICKY
    }}

    override fun onDestroy() {{
        running = false
        worker?.interrupt()
        worker = null
        super.onDestroy()
    }}
}}
"""


def _plain_class(cls, desc, type_label):
    return f"""\
package com.zachery.xrwrist.phone

/**
 * {cls} — {desc}
 *
 * Type: {type_label}. Fill in the domain logic below; keep Android framework
 * calls on the main thread and blocking work on background threads.
{COUNCIL_CHECKLIST_KT}{HUMAN_NOTE_KT} */
class {cls} {{

    // TODO: add your fields here. Prefer immutable vals + explicit state
    // transitions over scattered mutable vars.

    /** TODO: describe what this does and which thread calls it. */
    fun update() {{
        // TODO: implement.
    }}
}}
"""


def _settings(cls, desc):
    return f"""\
package com.zachery.xrwrist.phone

import android.content.Context
import android.content.SharedPreferences

/**
 * {cls} — {desc}
 *
 * Persistent settings following the DevSettings pattern: SharedPreferences
 * backing, @Volatile lazy init, coerced ranges, documented defaults.
{COUNCIL_CHECKLIST_KT} */
object {cls} {{

    private const val PREFS = "xrwrist_{cls.lower()}"
    // TODO: add your keys, e.g. private const val K_ENABLED = "enabled"

    @Volatile private var prefs: SharedPreferences? = null

    fun init(ctx: Context) {{
        if (prefs == null) {{
            prefs = ctx.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        }}
    }}

    private fun p(): SharedPreferences =
        prefs ?: throw IllegalStateException("{cls}.init() not called")

    // TODO: add typed properties, e.g.:
    // var enabled: Boolean
    //     get() = p().getBoolean(K_ENABLED, true)
    //     set(v) = p().edit().putBoolean(K_ENABLED, v).apply()
}}
"""


TYPE_LABELS = {
    "renderer": "Renderer",
    "input": "Input Handler",
    "network": "Network Module",
    "panel": "UI Panel",
    "devtool": "DevTool",
    "settings": "Settings",
}


def generate_phone(name: str, ftype: str, description: str):
    """Return a list of {path, language, content} dicts for a Phone feature."""
    cls = _class_name(name)
    desc = description.strip() or f"{cls} ({TYPE_LABELS.get(ftype, ftype)})"
    base = "phone/app/src/main/java/com/zachery/xrwrist/phone"

    files = []
    if ftype == "panel":
        files.append({"path": f"{base}/{cls}Activity.kt",
                      "language": "kotlin",
                      "content": _ui_panel(cls, desc)})
        manifest_note = f"""\
# Integrating {cls}Activity into the Phone app

## AndroidManifest.xml
Register the activity inside `<application>`:

```xml
<activity
    android:name=". {cls}Activity"
    android:exported="false"
    android:theme="@android:style/Theme.Material.NoActionBar" />
```

## Launch it
From DevOptionsActivity (or wherever appropriate):

```kotlin
startActivity(Intent(this, {cls}Activity::class.java))
```
"""
        files.append({"path": f"phone/INTEGRATION_{cls.lower()}.md",
                      "language": "markdown", "content": manifest_note})
    elif ftype == "network":
        files.append({"path": f"{base}/{cls}Service.kt",
                      "language": "kotlin",
                      "content": _network_service(cls, desc)})
        manifest_note = f"""\
# Integrating {cls}Service into the Phone app

## AndroidManifest.xml
Register the service and its permissions inside `<application>`:

```xml
<service
    android:name=".{cls}Service"
    android:exported="false"
    android:foregroundServiceType="connectedDevice" />
```

## Start / stop
```kotlin
// start
ContextCompat.startForegroundService(ctx, Intent(ctx, {cls}Service::class.java))
// stop
ctx.stopService(Intent(ctx, {cls}Service::class.java))
```

Remember: a foreground service MUST call `startForeground()` with a
notification within ~5s of starting or the system kills it.
"""
        files.append({"path": f"phone/INTEGRATION_{cls.lower()}.md",
                      "language": "markdown", "content": manifest_note})
    elif ftype == "settings":
        files.append({"path": f"{base}/{cls}.kt",
                      "language": "kotlin",
                      "content": _settings(cls, desc)})
    else:
        files.append({"path": f"{base}/{cls}.kt",
                      "language": "kotlin",
                      "content": _plain_class(cls, desc,
                                              TYPE_LABELS.get(ftype, ftype))})
    return files
