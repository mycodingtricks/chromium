# Fingerprint surface inventory

The detectable surfaces a stealth browser must control, with the exact patch
point for each. **Line numbers drift between Chromium versions — always re-grep
the symbol name, never trust a line number from this doc.** The function/symbol
names are the stable anchors.

Legend for "mechanism":
- **flag** — an existing upstream command-line flag handles it; no patch needed.
- **hook** — small `// [stealth]` guard in the upstream getter → `stealth/src/`.
- **noise** — perturb output deterministically per-profile (seeded), not spoof.

---

## navigator.* (renderer / Blink core)

| Property | File (re-grep the symbol) | Symbol | Mechanism |
|---|---|---|---|
| `platform` | `core/frame/navigator_id.cc` | `NavigatorID::platform()` | hook |
| `appVersion` | `core/frame/navigator_id.cc` | `NavigatorID::appVersion()` | hook |
| `userAgent` | `core/execution_context/navigator_base.cc` | `NavigatorBase::userAgent()` | **flag** `--user-agent` first; hook for per-frame consistency |
| `userAgentData` (Client Hints) | `core/frame/navigator_ua_data.cc` | brands/platform/mobile | hook |
| `language` / `languages` | `core/frame/navigator_language.cc` | `NavigatorLanguage::language()` / `languages()` | **flag** `--lang` + `Accept-Language`; hook for JS array |
| `hardwareConcurrency` | `core/frame/navigator_concurrent_hardware.cc` | `NavigatorConcurrentHardware::hardwareConcurrency()` | hook |
| `deviceMemory` | `core/frame/navigator_device_memory.cc` | `NavigatorDeviceMemory::deviceMemory()` | hook |
| `maxTouchPoints` | `core/events/navigator_events.cc` | `NavigatorEvents::maxTouchPoints(Navigator&)` | hook |
| `webdriver` | `core/frame/navigator.cc` | `Navigator::webdriver()` | **must be false**; hook or avoid `--enable-automation` |
| `plugins` / `mimeTypes` | `modules/plugins/dom_plugin_array.cc` | enumeration | hook (return a canonical set) |

## screen / window

| Property | File | Symbol | Mechanism |
|---|---|---|---|
| `screen.width/height` | `core/frame/screen.cc` | `Screen::width()` / `height()` | hook |
| `screen.availWidth/Height` | `core/frame/screen.cc` | `Screen::availWidth()` … | hook |
| `screen.colorDepth/pixelDepth` | `core/frame/screen.cc` | `Screen::colorDepth()` | hook |
| `devicePixelRatio` | `core/frame/local_dom_window.cc` | `devicePixelRatio()` | **flag** `--force-device-scale-factor` |
| outer/inner window size | window sizing | — | **launcher** sets `--window-size`/`--window-position` |

## Canvas (2D + WebGL readback) — *noise, not spoof*

The standard technique is a tiny, **per-profile-seeded** perturbation so the hash
is stable for a profile but differs across profiles, while staying visually
identical.

| Surface | File | Symbol | Mechanism |
|---|---|---|---|
| `canvas.toDataURL` / `toBlob` | `core/html/canvas/html_canvas_element.cc` | `ToDataURLInternal` | noise on pixel buffer |
| `getImageData` | `modules/canvas/canvas2d/base_rendering_context_2d.cc` | `getImageData` | noise |
| WebGL `readPixels` | `modules/webgl/webgl_rendering_context_base.cc` | `readPixels` | noise |
| WebGL `getParameter` (UNMASKED_VENDOR_WEBGL / UNMASKED_RENDERER_WEBGL) | `modules/webgl/webgl_rendering_context_base.cc` | `getParameter` (search `UNMASKED`) | hook (spoof GPU strings) |
| WebGL supported extensions / precision | same file | `getSupportedExtensions`, `getShaderPrecisionFormat` | hook |

## Audio — *noise*

| Surface | File | Symbol | Mechanism |
|---|---|---|---|
| AudioContext fingerprint | `modules/webaudio/` (offline render path, `audio_destination_handler.cc`) | render output | seeded noise on output samples |
| `AudioContext.baseLatency` / sampleRate | `modules/webaudio/audio_context.cc` | getters | hook |

## Fonts

| Surface | File | Mechanism |
|---|---|---|
| Font enumeration / metrics | `platform/fonts/` (font matching), `modules/font_access/` | restrict to a profile-defined font set; the launcher can also control which fonts the OS exposes |
| Local font access API | `modules/font_access/` | hook to filter the enumerated list |

## WebRTC (IP leak is the big one)

| Surface | Mechanism |
|---|---|
| Local/public IP via ICE | **flag first:** `--force-webrtc-ip-handling-policy=disable_non_proxied_udp` (`content_switches.cc: kForceWebRtcIPHandlingPolicy`) + route through the profile's proxy |
| Media device enumeration (`enumerateDevices`) | hook in `modules/mediastream/` to return a profile-consistent device list |
| RTC stats / codecs | usually left stock; only patch if a target detector reads them |

## Timezone / locale / geo

| Surface | Mechanism |
|---|---|
| `Intl` / `Date` timezone | **launcher sets `TZ` env var** + ICU default — do NOT patch V8 date code |
| Geolocation | **flag/policy** override or hook `device/geolocation`; feed profile lat/long |
| `Intl.DateTimeFormat().resolvedOptions()` locale | `--lang` + `TZ`; consistency check against UA |

## Misc / lower priority

| Surface | Note |
|---|---|
| `getBoundingClientRect` / `getClientRects` | sub-pixel noise (`core/dom/element.cc`, search `getBoundingClientRect`) — only if targeted |
| Battery API | spoof or disable (`modules/battery/`) |
| Gamepad, Bluetooth, USB, Serial | usually disable via features for consistency |
| `performance.now` resolution | already coarsened upstream; leave alone |
| WebGPU adapter info | `modules/webgpu/` — emerging surface, spoof vendor/architecture |

---

## Consistency is everything

A detector flags **inconsistency**, not any single value. The config layer
(`stealth/src/fingerprint/`) must enforce cross-surface coherence, e.g.:
- `navigator.platform` ↔ UA OS ↔ `userAgentData.platform` ↔ font set ↔ WebGL renderer.
- `timezone` ↔ geolocation ↔ `Accept-Language`/`languages` ↔ IP geolocation.
- `hardwareConcurrency` / `deviceMemory` ↔ plausible device class ↔ GPU.

Treat the profile JSON as the single source of truth and derive everything from
it; never let two surfaces compute a value independently.
