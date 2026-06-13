// Copyright 2026 The Stealth Browser Authors.
//
// Per-process holder of the active fingerprint profile. This is the "brains"
// every upstream // [stealth] hook calls into. It lives entirely under stealth/
// so it never conflicts on a Chromium rebase.
//
// Process model (see ../../docs/architecture.md):
//   - Browser process loads the profile JSON once (from --stealth-profile).
//   - The value is forwarded to child processes via command-line switches
//     (scalars) and/or Mojo (structured data).
//   - Each process parses it into the singleton returned by Get().
//
// Keep this layer free of Blink (WTF::String) and //content types so it can be
// depended on from any layer. Hook sites convert std::string -> WTF::String.

#ifndef STEALTH_SRC_FINGERPRINT_FINGERPRINT_CONFIG_H_
#define STEALTH_SRC_FINGERPRINT_FINGERPRINT_CONFIG_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace base {
class CommandLine;
}  // namespace base

namespace stealth {

// The orchestrator passes the per-launch profile to the binary via this switch.
// The value is either a path to a JSON file or an inline JSON blob (a string
// whose first non-whitespace character is '{'). We own this constant in
// stealth/ rather than registering it in upstream switches.cc, which keeps the
// upstream footprint minimal; forwarding to child processes (a separate task)
// re-emits it onto child command lines.
inline constexpr char kStealthProfileSwitch[] = "stealth-profile";

// Discrete scalar switches (optional fast path). Instead of, or in addition to,
// a full --stealth-profile JSON, the orchestrator may pass individual
// fingerprint scalars directly. When both are supplied, a discrete switch
// OVERRIDES the corresponding profile field (it is the more specific, explicit
// per-launch value). These are also the surfaces that get forwarded to child
// processes as plain command-line scalars, so the same switch names double as
// the browser->renderer/worker forwarding channel. We own all of them in
// stealth/ and read them directly, so no upstream switches.cc edit is needed.
inline constexpr char kStealthUaSwitch[] = "stealth-ua";
inline constexpr char kStealthPlatformSwitch[] = "stealth-platform";
inline constexpr char kStealthLanguageSwitch[] = "stealth-language";
// Comma-separated, e.g. --stealth-languages=en-US,en
inline constexpr char kStealthLanguagesSwitch[] = "stealth-languages";
inline constexpr char kStealthHardwareConcurrencySwitch[] =
    "stealth-hardware-concurrency";
inline constexpr char kStealthDeviceMemorySwitch[] = "stealth-device-memory";
inline constexpr char kStealthMaxTouchPointsSwitch[] =
    "stealth-max-touch-points";
// "WIDTHxHEIGHT" or "WIDTHxHEIGHTxDEPTH", e.g. --stealth-screen=1920x1080x24
inline constexpr char kStealthScreenSwitch[] = "stealth-screen";
inline constexpr char kStealthColorDepthSwitch[] = "stealth-color-depth";
inline constexpr char kStealthWebglVendorSwitch[] = "stealth-webgl-vendor";
inline constexpr char kStealthWebglRendererSwitch[] = "stealth-webgl-renderer";
// Hashed into the u64 noise seed exactly like the JSON "seed" field, so a UUID
// passed via either channel yields the same deterministic noise.
inline constexpr char kStealthSeedSwitch[] = "stealth-seed";

// Noise toggles. Valueless (presence => enabled). They originate from the
// profile JSON (canvas/audio/webgl ".noise" booleans); there is no separate
// "fast path" for them, but they ARE part of the browser->child forwarding
// channel (AppendChildSwitches) so worker-scope noise surfaces (OffscreenCanvas
// / WebGL in workers) stay identical to the window.
inline constexpr char kStealthCanvasNoiseSwitch[] = "stealth-canvas-noise";
inline constexpr char kStealthAudioNoiseSwitch[] = "stealth-audio-noise";
inline constexpr char kStealthWebglNoiseSwitch[] = "stealth-webgl-noise";

// All getters return std::nullopt when the profile does not override that
// surface, so the hook falls through to stock Chromium behaviour:
//
//   if (auto v = stealth::FingerprintConfig::Get().Platform()) return *v;
//   // ... original upstream body ...
class FingerprintConfig {
 public:
  // Process-wide singleton. Empty (all-nullopt) until Initialize() runs.
  static FingerprintConfig& Get();

  // Parse a profile JSON document (the --stealth-profile file contents).
  // Safe to call once per process during early startup. Returns false on
  // malformed input, leaving the config empty (= stock behaviour).
  bool InitializeFromJson(const std::string& json);

  // Browser-process ingestion entry point. Reads the --stealth-profile switch
  // from `command_line` and loads it into this config exactly once. The switch
  // value may be EITHER a filesystem path to a JSON profile OR an inline JSON
  // blob (detected when the first non-whitespace character is '{'). Returns
  // false (leaving the config empty = stock behaviour) when the switch is
  // absent, the file cannot be read, or the JSON is malformed. Renderers must
  // NOT call this — they are sandboxed and cannot read the file; the browser
  // forwards values to them (see AppendExtraCommandLineSwitches).
  bool InitializeFromCommandLine(const base::CommandLine& command_line);

  // Worker-scope parity: forward this (already-resolved) identity onto a child
  // process's command line as discrete --stealth-* scalars. Called by the
  // browser's AppendExtraCommandLineSwitches hook for EVERY child process so
  // that the window, every worker scope (dedicated/shared/service — all hosted
  // in renderer processes), the GPU process (WebGL vendor/renderer) and utility
  // processes all read one coherent identity. Children never read the profile
  // file; they receive only these scalars (no --stealth-profile, so no
  // sandboxed file access is attempted). No-op when the config is inactive. A
  // value the browser set but a child missed would surface as a CreepJS lie.
  void AppendChildSwitches(base::CommandLine* command_line) const;

  // True once a non-empty profile has been loaded.
  bool active() const { return active_; }

  // Stable per-profile seed for deterministic noise (canvas/audio/webgl).
  std::optional<uint64_t> NoiseSeed() const { return noise_seed_; }

  // --- navigator ---
  std::optional<std::string> UserAgent() const { return user_agent_; }
  std::optional<std::string> Platform() const { return platform_; }
  std::optional<std::string> Language() const { return language_; }
  std::optional<std::vector<std::string>> Languages() const { return languages_; }
  std::optional<unsigned> HardwareConcurrency() const { return hw_concurrency_; }
  std::optional<float> DeviceMemory() const { return device_memory_; }
  std::optional<int> MaxTouchPoints() const { return max_touch_points_; }

  // --- screen ---
  std::optional<int> ScreenWidth() const { return screen_width_; }
  std::optional<int> ScreenHeight() const { return screen_height_; }
  std::optional<unsigned> ColorDepth() const { return color_depth_; }

  // --- webgl ---
  std::optional<std::string> WebglVendor() const { return webgl_vendor_; }
  std::optional<std::string> WebglRenderer() const { return webgl_renderer_; }

  // --- noise toggles ---
  bool CanvasNoise() const { return canvas_noise_; }
  bool AudioNoise() const { return audio_noise_; }
  bool WebglNoise() const { return webgl_noise_; }

 private:
  FingerprintConfig();
  ~FingerprintConfig();

  // Applies discrete --stealth-* scalar switches on top of whatever the profile
  // JSON (if any) set. A present switch overrides the corresponding field.
  // Returns true if at least one recognised switch was applied.
  bool ApplyCommandLineScalars(const base::CommandLine& command_line);

  // Guards InitializeFromCommandLine against running twice. The browser inits
  // explicitly+early in CreateBrowserMainParts; Get() also inits lazily on
  // first touch (the sole ingestion point in child processes). Idempotent so
  // the two paths never double-parse.
  bool initialized_ = false;
  bool active_ = false;
  std::optional<uint64_t> noise_seed_;
  // The raw seed string as supplied by the orchestrator (JSON "seed" or
  // --stealth-seed), retained so AppendChildSwitches can forward the SAME
  // string to children; they re-hash it via HashSeed to the identical u64
  // noise_seed_. (We cannot forward the hashed u64, as the child would hash it
  // again and diverge.)
  std::optional<std::string> seed_string_;

  std::optional<std::string> user_agent_;
  std::optional<std::string> platform_;
  std::optional<std::string> language_;
  std::optional<std::vector<std::string>> languages_;
  std::optional<unsigned> hw_concurrency_;
  std::optional<float> device_memory_;
  std::optional<int> max_touch_points_;

  std::optional<int> screen_width_;
  std::optional<int> screen_height_;
  std::optional<unsigned> color_depth_;

  std::optional<std::string> webgl_vendor_;
  std::optional<std::string> webgl_renderer_;

  bool canvas_noise_ = false;
  bool audio_noise_ = false;
  bool webgl_noise_ = false;
};

}  // namespace stealth

#endif  // STEALTH_SRC_FINGERPRINT_FINGERPRINT_CONFIG_H_
