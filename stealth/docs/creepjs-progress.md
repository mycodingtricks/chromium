# CreepJS evasion — implementation progress (living memory)

**Purpose.** This is the persistent task queue + status log for making this fork
pass CreepJS (`abrahamjuliot.github.io/creepjs`) with a high trust grade, **0
lies**, and a low bot score. Read `docs/creepjs-system-prompt.md` for the rules.
Update this file after every change. Nothing here is implemented yet — this is
the full surface inventory CreepJS tracks, ordered by priority.

**Status legend:** `TODO` · `WIP` · `DONE` · `BLOCKED` · `FLAG` (an existing
Chromium flag handles it; verify, don't patch).

**Per-DONE item, record:** CreepJS surface · upstream patch point (symbol, not
line) · `stealth/src/` file · mechanism (flag/hook/noise) · last observed CreepJS
result.

---

## How CreepJS scores you (keep this in mind for every task)

- It compares **window vs Dedicated/Shared/Service Worker vs phantom iframe**.
  Any mismatch = a **lie**. Consistency across contexts beats any single value.
- Its **lies engine** inspects native functions for tampering (toString shape,
  descriptors, ownKeys, prototype chain, Proxy-via-stack, `class extends`,
  constructor TypeErrors) inside a pristine iframe. → **Spoof in C++, never
  monkeypatch JS in-page or via extension.**
- It builds two hashes: `fp` (everything) and `creep` (hardened/stable — lies and
  RFP-volatile surfaces dropped). Target: a stable, internally-consistent `creep`
  hash + 0 lies.
- When a surface is lied about it sets `LowerEntropy.*` and drops that surface —
  so a detected lie both hurts the grade and destabilizes the hash.

---

## P0 — Args ingestion contract (the input pipe; build this first)

The fingerprint values are supplied per launch by an external **orchestration
platform** via command-line args (CloakBrowser-style). The browser consumes them
and enforces consistency — it never invents identities.

- [x] **`--stealth-profile` ingestion (file + inline)** `DONE` — browser process
  parses the orchestrator's profile **once** into `stealth::FingerprintConfig`.
  Accepts both a path and an inline JSON blob (inline detected when first
  non-whitespace char is `{`; otherwise read as a file via `base::ReadFileToString`).
  - CreepJS surface: n/a (input plumbing; no directly-observable surface until
    navigator/screen hooks land and read the config).
  - Upstream patch point: `ChromeContentBrowserClient::CreateBrowserMainParts`
    (browser-only, runs once) + `source_set("core")` deps in
    `chrome/browser/BUILD.gn`.
  - stealth/src file: `stealth/src/fingerprint/fingerprint_config.{h,cc}`
    (`InitializeFromCommandLine`, `kStealthProfileSwitch`).
  - Mechanism: hook (calls net-new ingestion code). We own the switch constant in
    `stealth/src/` and read it directly, so **no `switches.cc`/`content_switches.cc`
    edit was needed** — keeps the upstream footprint minimal. Upstream registration
    is deferred to the child-forwarding task, which re-emits the switch onto child
    command lines.
  - Last observed CreepJS result: not yet built — depot_tools not on PATH and no
    `out/` dir; a clean build is multi-hour. Verify once the first navigator hook
    makes the config observable.
- [x] **Discrete scalar switches (optional fast path)** `DONE` — the
  orchestrator can pass common scalars directly in addition to / instead of the
  JSON: `--stealth-ua`, `--stealth-platform`, `--stealth-language`,
  `--stealth-languages` (comma-separated), `--stealth-hardware-concurrency`,
  `--stealth-device-memory`, `--stealth-max-touch-points`, `--stealth-screen`
  (`WxH[xDepth]`), `--stealth-color-depth`, `--stealth-webgl-vendor`,
  `--stealth-webgl-renderer`, `--stealth-seed`. A present scalar OVERRIDES the
  corresponding profile field; a scalar may also activate the config with no
  profile present. `--stealth-seed` is hashed via the shared `HashSeed` (FNV-1a)
  so a UUID via either channel produces the same noise seed. These switch names
  double as the browser→child forwarding channel (next task).
  - CreepJS surface: n/a (input plumbing; observable only once navigator/screen/
    webgl hooks land and read the config).
  - Upstream patch point: none — all net-new in `stealth/src/`; no upstream edit
    (we own the switch constants in stealth/, read them directly).
  - stealth/src file: `stealth/src/fingerprint/fingerprint_config.{h,cc}`
    (`ApplyCommandLineScalars`, `HashSeed`, `kStealth*Switch` constants;
    `InitializeFromCommandLine` now parses profile then layers scalars).
  - Mechanism: flag (discrete `--stealth-*` scalars read directly into config).
  - Last observed CreepJS result: not yet built — depot_tools not on PATH and no
    `out/` dir; clean build is multi-hour. Verify once the first navigator hook
    makes the config observable (same gate as `--stealth-profile` ingestion).
- [x] **Orchestrator owns values; browser enforces** `DONE` — audited the entire
  net-new tree (`find stealth/src`): the only brain is
  `fingerprint_config.{h,cc}` and the only upstream hooks are the single
  browser-process ingestion call + the `BUILD.gn` dep. Findings:
  - *No randomness*: grep `rand|random|mt19937|RandUint|RandDouble|RandInt` over
    `stealth/src` → 0 hits. The only entropy-shaped op is `HashSeed` (pure
    deterministic FNV-1a over the orchestrator-supplied seed string); no
    `Date`/RNG/launch-time entropy is folded in anywhere.
  - *No hardcoded identities*: grep `Mozilla|Win32|MacIntel|Google Inc|ANGLE|
    NVIDIA|en-US|America/|SwiftShader|…` → only a comment (`en-US` example) and
    the three noise-toggle `value_or(false)` booleans. No identity field has a
    baked-in default value.
  - *Absent ⇒ nullopt ⇒ stock*: every identity getter returns its
    `std::optional<…>` member, all default-constructed to `std::nullopt`; the
    only non-optional members are the three noise toggles (`bool … = false` =
    noise-off = stock fallthrough).
  - *Same args ⇒ same fingerprint*: parsing is deterministic (JSON read + scalar
    overrides), no launch-time entropy.
  - Carry-forward rule for every future surface hook: read from a
    `FingerprintConfig` getter, fall through on `nullopt`, **never compute a
    value locally** — this audit must be re-run as navigator/screen/webgl/noise
    hooks land (none exist yet, so the guarantee is currently trivial+total).
  - CreepJS surface: n/a (invariant audit; not directly observable until surface
    hooks exist). Upstream patch point:
    `ChromeContentBrowserClient::CreateBrowserMainParts` (sole ingestion site).
    stealth/src file: `stealth/src/fingerprint/fingerprint_config.{h,cc}`.
    Mechanism: hook/audit (no new code; verification of the existing pipe).
    Last observed CreepJS result: not yet observable (no surface hooks; same
    build gate as the ingestion tasks — depot_tools/`out/` absent, multi-hour
    clean build).
- [ ] **Noise seed from args** `TODO` — `seed` / `--stealth-seed` drives all
  seeded noise (canvas/audio/WebGL/DOMRect) so a given identity is stable across
  launches and distinct across identities.

## P0 — Consistency backbone (do these first; everything else depends on parity)

- [ ] **Worker-scope parity** `TODO` — the single most important task. Every value
  below that is readable in a worker MUST be identical in window, Dedicated,
  Shared, and Service worker scopes. Verify the command-line/Mojo forwarding in
  `chrome_content_browser_client.cc::AppendExtraCommandLineSwitches` reaches
  *worker and service-worker* render processes, not just the main frame.
  Cross-checked by CreepJS: `userAgent`, `platform`, `hardwareConcurrency`,
  `deviceMemory`, `language`/`languages`, timezone, Intl locale, JS engine,
  `userAgentData`, WebGL VENDOR/RENDERER, `Function.toString` integrity.
- [ ] **Single profile source of truth** `TODO` — confirm every surface derives
  from the one profile JSON in `stealth/src/fingerprint/`; no surface computes a
  shared fact independently.

## P1 — Navigator & client hints (high entropy, heavily cross-checked)

- [ ] **navigator.userAgent** `TODO` — `--user-agent` flag + per-frame/worker
  consistency hook (`NavigatorBase::userAgent()` in
  `core/execution_context/navigator_base.cc`). UA must contain appVersion; no
  whitespace/gibberish; UA-OS must match platform.
- [ ] **navigator.platform** `TODO` — hook `NavigatorID::platform()`
  (`navigator_id.cc`). Must match UA OS + `userAgentData.platform`.
- [ ] **navigator.appVersion** `TODO` — hook `NavigatorID::appVersion()`. UA must
  contain it.
- [ ] **navigator.userAgentData** (+ `getHighEntropyValues`) `TODO` — hook
  `navigator_ua_data.cc`: brands, platform, platformVersion, architecture,
  bitness, model, mobile, uaFullVersion. Must agree with UA. Windows version is
  read from `platformVersion`.
- [ ] **navigator.language / languages** `TODO` — `--lang` + `Accept-Language`
  flag; hook `navigator_language.cc` for the JS array. languages[0] must prefix
  language; aligns with timezone/locale/voices.
- [ ] **navigator.hardwareConcurrency** `TODO` — hook
  `NavigatorConcurrentHardware::hardwareConcurrency()`. Plausible device class.
- [ ] **navigator.deviceMemory** `TODO` — hook `navigator_device_memory.cc`. Must
  be ∈ {0.25,0.5,1,2,4,8,16,32} and plausible vs `performance.memory`.
- [ ] **navigator.maxTouchPoints** `TODO` — hook
  `NavigatorEvents::maxTouchPoints(Navigator&)` in `core/events/navigator_events.cc`.
  0 for desktop; >0 must agree with pointer/hover media + mobile UA.
- [ ] **navigator.plugins / mimeTypes** `TODO` — hook `dom_plugin_array.cc`. Return
  the canonical Chrome PDF plugin set (empty = headless tell). plugin↔mimeType
  cross-referenced; array instance types checked.
- [ ] **navigator.vendor / oscpu / doNotTrack / globalPrivacyControl** `TODO` —
  vendor "Google Inc." for Blink; oscpu Firefox-only (must be absent on Blink).
- [ ] **navigator.gpu (WebGPU)** `TODO` — `modules/webgpu/`: adapter
  limits/features are hashed. Spoof vendor/architecture consistently with WebGL.
- [ ] **navigator.bluetooth / permissions / connection** `TODO` —
  `bluetooth.getAvailability()`, `permissions.query()` results, `connection.{type,
  rtt,downlink,downlinkMax}`. Keep consistent; missing downlinkMax is a tell.

## P1 — Screen & window

- [ ] **screen.width/height/availWidth/availHeight** `TODO` — hook `screen.cc`.
  MUST satisfy `matchMedia("(device-width)/(device-height)")`. Provide a believable
  taskbar delta — `availWidth==width && availHeight==height` (no taskbar) is a
  headless/low-entropy tell when width>800.
- [ ] **screen.colorDepth / pixelDepth** `TODO` — hook `Screen::colorDepth()`
  (typically 24).
- [ ] **screen.orientation.type** `TODO` — must match landscape/portrait vs
  dimensions.
- [ ] **devicePixelRatio** `FLAG` — `--force-device-scale-factor`. Must satisfy
  `matchMedia("(resolution:Ndppx)")`.
- [ ] **inner/outer width/height, visualViewport** `FLAG` — launcher
  `--window-size`/`--window-position`. viewport==screen is a headless tell.

## P1 — GPU: WebGL (high entropy, predicts device)

- [ ] **WebGL UNMASKED_VENDOR_WEBGL / UNMASKED_RENDERER_WEBGL** `TODO` — hook
  `webgl_rendering_context_base.cc::getParameter` (search `UNMASKED`). Renderer
  string MUST be in CreepJS's known-GPU list and identical window↔worker.
  SwiftShader = headless.
- [ ] **WebGL getParameter (full numeric set)** `TODO` — VENDOR, RENDERER, VERSION,
  SHADING_LANGUAGE_VERSION + ~50 MAX_*/range params (see research: MAX_TEXTURE_SIZE,
  MAX_VIEWPORT_DIMS, MAX_VERTEX_ATTRIBS, MAX_*_UNIFORM_*, etc.). WebGL1 vs WebGL2
  values must be mutually consistent. Keep stock unless they betray the spoofed GPU.
- [ ] **WebGL getSupportedExtensions** `TODO` — must match the claimed GPU/driver.
- [ ] **WebGL getShaderPrecisionFormat** `TODO` — VERTEX/FRAGMENT ×
  LOW/MEDIUM/HIGH FLOAT/INT.
- [ ] **WebGL readPixels / toDataURL** `TODO` — seeded noise, engine-baseline
  consistent.

## P2 — Noise surfaces (seeded, stable-per-profile, engine-baseline-matching)

- [ ] **Canvas 2D** `TODO` — noise in `html_canvas_element.cc::ToDataURLInternal`
  and `base_rendering_context_2d.cc::getImageData`. CreepJS pixel-diffs two
  identical renders (tracks which RGBA channels move), compares to BLINK baseline,
  checks emoji measureText set, and checks **TextMetrics floats-vs-integers**
  (`measureText` → actualBoundingBox*, fontBoundingBox*, width). Noise must keep
  metrics integer-shaped where Blink emits integers.
- [ ] **Audio** `TODO` — seeded noise on OfflineAudioContext render output
  (`modules/webaudio/`, offline render path). CreepJS sums float
  frequency/time-domain data + compressor reduction, matches per-browser patterns,
  injects buffer values to confirm persistence, and flags all-unique-5000 samples.
  Also keep AnalyserNode/Oscillator/DynamicsCompressor/BiquadFilter node default
  values stock.
- [ ] **DOMRect** `TODO` — sub-pixel seeded noise in `element.cc`
  (`getBoundingClientRect`/`getClientRects`) + Range variants. Must preserve
  `right-left===width` and `bottom-top===height` invariants and the +1px unshift
  recompute; `#cRect11`===`#cRect12`. Couple with canvas/SVG emoji metrics.
- [ ] **SVG metrics** `TODO` — `getBBox`, `getExtentOfChar`, `getSubStringLength`,
  `getComputedTextLength`. Same emoji/text metric family as Canvas+DOMRect — keep
  the three coherent.

## P2 — Fonts

- [ ] **Font enumeration / metrics** `TODO` — restrict to a profile font set
  (`platform/fonts/` matching, `modules/font_access/`). CreepJS detects fonts via
  `FontFace.load()`, `document.fonts.check()`, AND emoji pixel measurement, then
  infers OS/version from the font list. The set MUST match the claimed OS (don't
  expose macOS fonts on a Windows profile).

## P2 — Timezone / Intl / locale / voices (tightly cross-checked)

- [ ] **Timezone** `FLAG` — set `TZ` env var (launcher) + ICU default; do NOT
  patch V8 date code. CreepJS cross-checks `Date.getTimezoneOffset()` vs
  `Intl.DateTimeFormat().resolvedOptions().timeZone`, and uses a **historical
  offset (year 1113)** + 400-city binary search to infer real location.
- [ ] **Intl locale parity** `TODO` — `Collator, DateTimeFormat, DisplayNames,
  ListFormat, NumberFormat, PluralRules, RelativeTimeFormat` `.resolvedOptions()
  .locale` must all agree (and with `--lang`). Conflict = lie.
- [ ] **speechSynthesis voices** `TODO` — `modules/speech/`. `getVoices()` default
  voice lang MUST match `Intl` locale (explicit CreepJS check
  `voiceLangMismatch`). Provide an OS-plausible voice list.
- [ ] **Geolocation** `FLAG/hook` — feed profile lat/long; must agree with
  timezone + IP geolocation + locale.

## P2 — Engine quirks (must look like real Blink/V8; usually leave stock)

- [ ] **Math results** `TODO` — verify per-engine trig/hyperbolic/exp/log edge
  cases match real Blink/V8 baselines (don't break these with any V8 patching).
- [ ] **Error name/message strings** `TODO` — engine-specific; leave stock.
- [ ] **Console/engine errors** (malformed `new Function()` snippets) `TODO` —
  leave stock.
- [ ] **CSS computed-style property list** `TODO` — full CSSStyleDeclaration
  enumeration reveals engine/version; leave stock, just ensure version matches UA.
- [ ] **System colors (36) & system fonts (6)** `TODO` — `ActiveText` ===
  `rgb(255,0,0)` is a headless tell; ensure normal desktop values.
- [ ] **CSS media features** `TODO` — `prefers-color-scheme`,
  `prefers-reduced-motion`, `forced-colors`, `any-pointer`/`pointer`,
  `any-hover`/`hover`, `color-gamut`, `monochrome`, etc. Pointer/hover must agree
  with maxTouchPoints/mobile.
- [ ] **Features API / window keys / HTMLElement keys** `TODO` — exposed API
  surface reveals engine + version; keep matching the claimed Chrome version.

## P2 — Media / WebRTC

- [ ] **Media capabilities/codecs** `TODO` — `canPlayType`, `MediaSource`/
  `MediaRecorder.isTypeSupported` for the 12 CreepJS MIME strings. Must match the
  claimed Chrome build (proprietary codec support differs Chrome vs Chromium —
  watch this).
- [ ] **WebRTC IP leak** `FLAG` — `--force-webrtc-ip-handling-policy=
  disable_non_proxied_udp` + route through profile proxy. Big one.
- [ ] **enumerateDevices** `TODO` — hook `modules/mediastream/`: return a
  profile-consistent audio-in/out + video-in device list (0 devices is a tell).
- [ ] **WebRTC SDP / codecs / extensions** `TODO` — usually stock; only patch if a
  target detector reads the SDP codec/fmtp/extmap details.

## P3 — Device status

- [ ] **Battery API** `TODO` — spoof or disable (`modules/battery/`) consistently.
- [ ] **performance.memory** `TODO` — jsHeapSizeLimit must be plausible vs
  deviceMemory.
- [ ] **storage quota / navigator.connection / timer resolution** `TODO` — keep
  defaults coherent; don't over-coarsen timers (RFP tell).

## P0 (cross-cutting) — Headless / automation / lie-engine hygiene

- [ ] **navigator.webdriver === false** `TODO` — hook `Navigator::webdriver()` in
  `core/frame/navigator.cc`, or never pass `--enable-automation`.
- [ ] **No HeadlessChrome in UA** (window AND worker) `TODO`.
- [ ] **window.chrome present & realistic** `TODO` — `chrome.runtime` proto must
  not look proxied (`hasBadChromeRuntime`); `chrome` not at a high prop index.
- [ ] **pdfViewerEnabled === true; non-empty plugins/mimeTypes** `TODO`.
- [ ] **Notification permission behavior correct** `TODO` — 'denied' default is a
  headless tell (`hasPermissionsBug`).
- [ ] **Real GPU, not SwiftShader** `TODO` — ties to WebGL renderer task.
- [ ] **No Proxy/toString tampering anywhere** `TODO` — this is automatic IF all
  spoofing is done in C++ and no JS natives are monkeypatched. Audit that we never
  inject page scripts that wrap natives. `hasToStringProxy`/`hasIframeProxy` must
  be false.
- [ ] **Don't trip resistance detection** `TODO` — we are NOT Tor/Firefox-RFP/
  Brave. Don't disable WebGL2/WASM/OfflineAudioContext/RTCRtpTransceiver or
  coarsen timers in the RFP-signature way, or CreepJS will misclassify us as a
  privacy browser (lowers trust, destabilizes hash).

## P3 — Prediction (informational, no direct action)

- CreepJS reverse-looks-up each surface hash against a crowd dataset to predict
  device/OS/GPU. No task here beyond: the predicted device should match the
  claimed profile (it will, if all the above are consistent).

---

## Change log (append newest at top)

- _2026-06-13_ — **DONE: Orchestrator owns values; browser enforces (audit).**
  Audited the whole net-new tree: 0 randomness sources, 0 hardcoded identities,
  every identity getter defaults to `std::nullopt` (absent⇒stock), parsing fully
  deterministic (same args⇒same fingerprint). Only entropy-shaped op is the
  deterministic `HashSeed` (FNV-1a). Established the carry-forward rule for all
  future surface hooks (read getter → fall through on nullopt → never compute
  locally) and noted the audit must be re-run as each surface lands. No code
  change; verification only. Not yet CreepJS-observable (no surface hooks).
- _2026-06-13_ — **DONE: `--stealth-profile` ingestion (file + inline).** Added
  `FingerprintConfig::InitializeFromCommandLine` + `kStealthProfileSwitch` in
  `stealth/src/fingerprint/`; detects inline JSON (`{`…) vs. file path and parses
  once. Hooked `ChromeContentBrowserClient::CreateBrowserMainParts` (browser-only,
  single call) and added `//stealth/src:fingerprint` to `source_set("core")` deps
  in `chrome/browser/BUILD.gn`. 2 patches recorded. No upstream `switches.cc` edit
  (we own the constant in stealth/). Not yet built/verified (no depot_tools/`out/`).
- _2026-06-13_ — Added the **P0 args-ingestion contract**: fingerprint values are
  supplied per launch by an external orchestration platform via command-line args
  (CloakBrowser-style); the browser ingests and enforces, never invents. Updated
  the system prompt with the input-contract / data-flow section.
- _2026-06-13_ — File created from CreepJS source research. All surfaces inventoried;
  nothing implemented yet. Next action: P0 args-ingestion pipe
  (`--stealth-profile` file+inline → `FingerprintConfig` → forward to every child),
  then worker-scope parity.
