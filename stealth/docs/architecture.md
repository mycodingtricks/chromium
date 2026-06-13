# Architecture: how a fingerprint profile reaches the page

A stealth browser serves **many identities from one binary**. The same compiled
Chrome must, on each launch, present a different & internally-consistent
fingerprint (UA, platform, screen, GPU, canvas noise, timezone, fonts, …). So the
design is **config-driven at runtime**, not hardcoded.

```
  launcher / orchestrator
        │  --stealth-profile=/path/to/profile.json   (one command-line switch)
        ▼
  ┌─────────────────────┐
  │  Browser process     │  reads JSON once at startup into a StealthConfig
  │  (chrome/browser)    │  object held in stealth/src/fingerprint/.
  └─────────┬───────────┘
            │  propagate to every child process. Two channels:
            │   (a) re-emit the switch onto the renderer command line
            │       (simple scalars: platform, UA, hw concurrency, tz), OR
            │   (b) Mojo, for large/structured data (font list, canvas seed).
            ▼
  ┌─────────────────────┐
  │  Renderer process    │  parses the same config into a per-process
  │  (Blink)             │  StealthConfig singleton.
  └─────────┬───────────┘
            │  Blink JS-facing getters consult the singleton instead of
            │  the real platform value.
            ▼
        navigator.platform, screen.width, canvas.toDataURL(), …
```

## Where each piece lives

| Concern | Location |
|---|---|
| Profile schema + sample profiles | `stealth/config/` |
| Config object, JSON parse, getters | `stealth/src/fingerprint/` (net-new, no rebase risk) |
| The `--stealth-profile` switch | declared in `stealth/src/`, **registered** via a 1-line hook in `third_party/blink/common/switches.cc` + `content/public/common/content_switches.cc` |
| Reading config in browser | hook in `chrome/browser/chrome_content_browser_client.cc` (`AppendExtraCommandLineSwitches` to forward to renderers) |
| Override hooks | 1-liners in the Blink files listed in `fingerprint-surfaces.md`, each calling into `stealth/src/fingerprint/` |

## The hook pattern (memorize this)

Every upstream touch should look like this — a guard that defers to our code and
otherwise falls through to stock behaviour:

```cpp
// third_party/blink/renderer/core/frame/navigator_id.cc
String NavigatorID::platform() const {
  // [stealth] override hook
  if (auto v = stealth::FingerprintConfig::Get().platform())
    return *v;
  // ... original upstream body unchanged ...
}
```

Why this shape:
- The diff is 2-3 lines → trivial to re-apply on upgrade.
- All real logic is in `stealth::FingerprintConfig`, a net-new file.
- `// [stealth]` marker makes every edit greppable: `git grep "\[stealth\]"`.

## Process-model gotchas

- **Renderers are sandboxed** and don't read files. Never `fopen` the profile in
  the renderer — pass values down from the browser via switches/Mojo.
- **Site isolation** spawns many renderers; every one must get the config, so the
  forwarding hook in the content browser client is mandatory, not optional.
- **GPU / utility / network** processes have their own fingerprint surfaces
  (WebGL vendor lives partly in the GPU process). Forward there too when needed.
- **V8 / timezone:** timezone is best handled with the standard `TZ` env var +
  ICU default locale rather than patching V8 date code. Set it in the launcher.
- **WebRTC IP leak:** handled via the `--force-webrtc-ip-handling-policy` and
  proxy settings, plus optionally patching `peerconnection`. Prefer flags first.

## Principle: prefer flags > config-driven C++ > hardcoded patches

For each surface, reach for the cheapest mechanism that works:
1. An existing Chromium command-line flag (zero patch).
2. A new flag/config read by net-new `stealth/src/` code via a tiny hook.
3. A larger upstream patch (last resort — these are the expensive ones to carry).
