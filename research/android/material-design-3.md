# Material Design 3 — Research Notes for XR Wrist Display Phone App

**Source:** https://developer.android.com/develop/ui/compose/designsystems/material3 (Material Design 3 in Compose)
**Companion:** https://m3.material.io (component specs), https://developer.android.com/develop/ui/views/theming/darktheme (dark theme guidance)
**Date accessed:** 2026-10-06
**Purpose:** Guide the developer-console / "Godmode" UI in the companion phone app (`com.zachery.xrwrist.phone`).

> All content summarized in my own words; short code/API references below are factual API names, not reproduced documentation text.

---

## 1. Dependency & theme entry point

```gradle
implementation "androidx.compose.material3:material3:$material3_version"
```

An M3 theme has three subsystems, passed into the `MaterialTheme` composable:

```kotlin
MaterialTheme(colorScheme = ..., typography = ..., shapes = ...) {
    // M3 app content
}
```

Note: some M3 APIs are still experimental and need `@OptIn(ExperimentalMaterial3Api::class)` at function or file level.

## 2. Color schemes (dynamic color)

- **Dynamic color (Material You)** is available on **Android 12+ only**. The algorithm derives custom colors from the user's wallpaper, generating light and dark schemes.
- Official fallback pattern — gate on `Build.VERSION_CODES.S` and fall back to static brand schemes:

```kotlin
val dynamicColor = Build.VERSION.SDK_INT >= Build.VERSION_CODES.S
val colors = when {
    dynamicColor && darkTheme -> dynamicDarkColorScheme(LocalContext.current)
    dynamicColor && !darkTheme -> dynamicLightColorScheme(LocalContext.current)
    darkTheme -> DarkColorScheme
    else -> LightColorScheme
}
```

- A color scheme is built from **five key colors**, each mapped to a tonal palette of 13 tones used by M3 components.
- **Color roles** to use (never hardcode colors; always pair `on*` with its surface): `primary`/`onPrimary`, `secondary`/`onSecondary`, `tertiary`/`onTertiary`, each with `*Container`/`on*Container` variants, plus `surface`, `onSurface`, `surfaceVariant`, `onSurfaceVariant`, `error`/`onError`. Use `MaterialTheme.colorScheme` to access them.
- Usage guidance from the docs: `primary` = main components (prominent buttons, active states, tint of elevated surfaces); `secondary` = less prominent components (e.g. filter chips); `tertiary` = contrasting accents.
- **Accessibility rule:** dynamic color is designed to meet contrast standards by default. When customizing, always pair correctly (`onPrimary` on `primary`, `onPrimaryContainer` on `primaryContainer`, etc.). A documented bad example: `tertiaryContainer` background with `primaryContainer` text = poor contrast.
- Brand scheme generator: the **Material Theme Builder** (material.io) exports ready-to-use `Color.kt` (all roles for light + dark) and `Theme.kt` (`lightColorScheme(...)` / `darkColorScheme(...)`) Compose code.

**Actionable for Godmode UI:** use the standard dynamic-color theme pattern above; pick brand colors via Theme Builder and keep both light/dark `ColorScheme`s; never hardcode colors in console/debug UI.

## 3. Typography scale (15 styles)

| Style | Default |
|---|---|
| `displayLarge` | Roboto 57/64 |
| `displayMedium` | Roboto 45/52 |
| `displaySmall` | Roboto 36/44 |
| `headlineLarge` | Roboto 32/40 |
| `headlineMedium` | Roboto 28/36 |
| `headlineSmall` | Roboto 24/32 |
| `titleLarge` | Roboto Medium 22/28 |
| `titleMedium` | Roboto Medium 16/24 |
| `titleSmall` | Roboto Medium 14/20 |
| `bodyLarge` | Roboto 16/24 |
| `bodyMedium` | Roboto 14/20 |
| `bodySmall` | Roboto 12/16 |
| `labelLarge` | Roboto Medium 14/20 |
| `labelMedium` | Roboto Medium 12/16 |
| `labelSmall` | Roboto Medium 11/16 |

- Model with the M3 `Typography` class; constructor has defaults so you only override slots you need. Access via `MaterialTheme.typography`, e.g. `Text(text = "...", style = MaterialTheme.typography.titleLarge)`.
- Recommendation: don't ship all 15 styles — pick the subset the product needs. The type scale is designed to scale across devices (e.g. Display Small can differ between phone/tablet).

**Actionable:** define a reduced `Typography` (title/body/label slots) for the dev console; use `bodyLarge`/`bodyMedium` for log readouts, `labelMedium`/`labelSmall` for status chips, `titleMedium` for section headers.

## 4. Shape scale

M3 `Shapes` has five slots: `extraSmall` (default 4.dp), `small` (8.dp), `medium` (12.dp), `large` (16.dp), `extraLarge` (24.dp), plus `RectangleShape` and `CircleShape`. Customize globally via `MaterialTheme(shapes = ...)` or per-component, e.g. `Card(shape = MaterialTheme.shapes.medium)`.

## 5. Elevation (tonal, not shadow)

- M3 elevation is primarily **tonal color overlays** (tone derived from the primary color) rather than shadows; dark-theme elevation overlays also changed to tonal overlays from the primary slot.
- `Surface` supports both: `tonalElevation` and `shadowElevation` parameters.

**Actionable:** prefer `tonalElevation` on cards/sheets in the console UI; avoid heavy shadow elevation (looks M2-era and breaks dark theme aesthetics).

## 6. Components relevant to a dev console / Godmode UI

Buttons (emphasis hierarchy — use in this order):
1. **Extended FAB** — highest-emphasis action (docs example: `ExtendedFloatingActionButton` with icon + text).
2. **Filled button** (`Button`) — high emphasis.
3. `FilledTonalButton` — medium emphasis.
4. `ElevatedButton`, `OutlinedButton`.
5. `TextButton` — lowest emphasis.

Cards: `Card` with `CardDefaults.cardColors(containerColor = ..., contentColor = ...)` and `CardDefaults.cardElevation(defaultElevation/pressedElevation/focusedElevation)`. Example pattern for a selected state: `primaryContainer` background + `onPrimaryContainer` text.

Dialogs: use `AlertDialog` for simple confirm/dismiss; custom `Dialog` composable for complex content (e.g. an in-app console input dialog).

Navigation: `NavigationBar` (≤5 destinations, compact), `NavigationRail` (small tablets / landscape), `NavigationDrawer` / `ModalNavigationDrawer` (larger screens). For a phone dev console, `NavigationBar` suffices.

Other M3 conveniences from docs: components derive tonal elevation from the color scheme automatically; ripple now uses a subtle "sparkle" (automatic on Android 12+ via platform RippleDrawable); overscroll stretch effect is default in `LazyColumn`/`LazyRow` with Compose Foundation 1.1.0+.

## 7. Dark theme guidance

- Dark theme available Android 10 (API 29)+. In Compose: follow the system via `isSystemInDarkTheme()` as the default for the `darkTheme` parameter; expose a user override (Light / Dark / System default) if needed.
- Never hardcode light-only colors/icons; always use theme attributes / color roles. Common pitfalls listed in docs: assuming light backgrounds, hardcoded text colors, hardcoded background + default text, static-color drawables.
- Notifications: use system-provided templates (e.g. `MessagingStyle`) so the system styles them; test custom notification views under both themes.
- Launch screens: no hardcoded white backgrounds; use `?android:attr/colorBackground`.
- Theme change = `uiMode` configuration change (activities recreated). Can opt into handling via `android:configChanges="uiMode"` and check `configuration.uiMode and Configuration.UI_MODE_NIGHT_MASK` (`UI_MODE_NIGHT_YES`/`UI_MODE_NIGHT_NO`).
- API 31+: prefer `UiModeManager.setApplicationNightMode` (lets system match theme during splash screen); API 30 and below: `AppCompatDelegate.setDefaultNightMode()`.

**Actionable:** Godmode/console screens must work in both themes from day one — all colors from `MaterialTheme.colorScheme`, type from `MaterialTheme.typography`. Default to system theme with a manual Light/Dark/System toggle in Developer Options.

## 8. Concise action list

1. Add `androidx.compose.material3:material3` dependency; opt into `ExperimentalMaterial3Api` where needed.
2. Implement `XrWristTheme` with the dynamic-color-when-available pattern (Android 12+), static light/dark brand `ColorScheme`s as fallback.
3. Use color-role pairs everywhere (`primary`/`onPrimary`, `surface`/`onSurface`, `*Container`/`on*Container`) — zero hardcoded colors.
4. Define a reduced `Typography` (title/body/label slots) and `Shapes` overrides once; consume via `MaterialTheme`.
5. Component mapping for Godmode: `Button`/`FilledTonalButton`/`TextButton` for actions, `Card` + `CardDefaults` for telemetry panels, `AlertDialog`/`Dialog` for confirms and the console input, `NavigationBar` for top-level console sections.
6. Prefer `tonalElevation` over shadows; test all screens in light + dark.
7. Offer Light/Dark/System theme override in Developer Options; use `UiModeManager.setApplicationNightMode` (API 31+) / `AppCompatDelegate.setDefaultNightMode()` (API ≤30).
