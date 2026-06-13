// Copyright 2026 The Stealth Browser Authors.

#include "stealth/src/fingerprint/fingerprint_config.h"

#include <string_view>

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/values.h"

namespace stealth {

namespace {

std::optional<std::string> GetString(const base::Value::Dict& d,
                                     std::string_view key) {
  const std::string* v = d.FindString(key);
  if (!v)
    return std::nullopt;
  return *v;
}

// FNV-1a hash of a (UUID-like) seed string into a u64. Used by both the JSON
// "seed" field and the --stealth-seed switch so either channel yields the same
// deterministic noise for a given seed.
uint64_t HashSeed(std::string_view seed) {
  uint64_t h = 1469598103934665603ULL;  // FNV-1a offset basis.
  for (char c : seed) {
    h ^= static_cast<uint8_t>(c);
    h *= 1099511628211ULL;  // FNV prime.
  }
  return h;
}

}  // namespace

FingerprintConfig::FingerprintConfig() = default;
FingerprintConfig::~FingerprintConfig() = default;

// static
FingerprintConfig& FingerprintConfig::Get() {
  static base::NoDestructor<FingerprintConfig> instance;
  return *instance;
}

bool FingerprintConfig::InitializeFromJson(const std::string& json) {
  std::optional<base::Value> parsed = base::JSONReader::Read(json);
  if (!parsed || !parsed->is_dict())
    return false;
  const base::Value::Dict& root = parsed->GetDict();

  // seed -> deterministic noise. We hash the string seed into a u64 so the
  // launcher can pass a human-readable UUID.
  if (const std::string* seed = root.FindString("seed"))
    noise_seed_ = HashSeed(*seed);

  if (const base::Value::Dict* nav = root.FindDict("navigator")) {
    user_agent_ = GetString(*nav, "userAgent");
    platform_ = GetString(*nav, "platform");
    language_ = GetString(*nav, "language");
    if (const base::Value::List* langs = nav->FindList("languages")) {
      std::vector<std::string> out;
      for (const base::Value& v : *langs) {
        if (v.is_string())
          out.push_back(v.GetString());
      }
      if (!out.empty())
        languages_ = std::move(out);
    }
    if (std::optional<int> v = nav->FindInt("hardwareConcurrency"))
      hw_concurrency_ = static_cast<unsigned>(*v);
    if (std::optional<double> v = nav->FindDouble("deviceMemory"))
      device_memory_ = static_cast<float>(*v);
    if (std::optional<int> v = nav->FindInt("maxTouchPoints"))
      max_touch_points_ = *v;
  }

  if (const base::Value::Dict* scr = root.FindDict("screen")) {
    if (std::optional<int> v = scr->FindInt("width"))
      screen_width_ = *v;
    if (std::optional<int> v = scr->FindInt("height"))
      screen_height_ = *v;
    if (std::optional<int> v = scr->FindInt("colorDepth"))
      color_depth_ = static_cast<unsigned>(*v);
  }

  if (const base::Value::Dict* gl = root.FindDict("webgl")) {
    webgl_vendor_ = GetString(*gl, "unmaskedVendor");
    webgl_renderer_ = GetString(*gl, "unmaskedRenderer");
    webgl_noise_ = gl->FindBool("noise").value_or(false);
  }
  if (const base::Value::Dict* c = root.FindDict("canvas"))
    canvas_noise_ = c->FindBool("noise").value_or(false);
  if (const base::Value::Dict* a = root.FindDict("audio"))
    audio_noise_ = a->FindBool("noise").value_or(false);

  active_ = true;
  return true;
}

bool FingerprintConfig::InitializeFromCommandLine(
    const base::CommandLine& command_line) {
  bool loaded = false;

  // 1) Full profile JSON (file or inline), if supplied. Sets the baseline.
  if (command_line.HasSwitch(kStealthProfileSwitch)) {
    // Detect inline JSON vs. a file path. JSON profiles are ASCII (UA strings
    // and the like are ASCII), so GetSwitchValueASCII is sufficient to peek at
    // the first non-whitespace character. A path that is non-ASCII yields an
    // empty ASCII value, which falls through to the path branch below.
    const std::string ascii =
        command_line.GetSwitchValueASCII(kStealthProfileSwitch);
    const std::string_view sv(ascii);
    const size_t first = sv.find_first_not_of(" \t\r\n");
    if (first != std::string_view::npos && sv[first] == '{') {
      if (InitializeFromJson(ascii))
        loaded = true;
      else
        LOG(ERROR) << "[stealth] inline --stealth-profile JSON is malformed";
    } else {
      // Treat the value as a path. Use the native path form so non-ASCII paths
      // (e.g. Windows wide paths) are handled correctly.
      const base::FilePath path =
          command_line.GetSwitchValuePath(kStealthProfileSwitch);
      if (path.empty()) {
        // Empty path: nothing to load from the profile channel.
      } else {
        std::string contents;
        if (!base::ReadFileToString(path, &contents)) {
          LOG(ERROR) << "[stealth] could not read --stealth-profile file: "
                     << path.value();
        } else if (InitializeFromJson(contents)) {
          loaded = true;
        } else {
          LOG(ERROR) << "[stealth] --stealth-profile file is not valid JSON: "
                     << path.value();
        }
      }
    }
  }

  // 2) Discrete scalar switches override / fill individual fields. A scalar may
  // be supplied with no profile at all, in which case it alone activates the
  // config.
  if (ApplyCommandLineScalars(command_line))
    loaded = true;

  return loaded;
}

bool FingerprintConfig::ApplyCommandLineScalars(
    const base::CommandLine& command_line) {
  bool applied = false;

  auto take_string = [&](const char* sw, std::optional<std::string>* out) {
    if (!command_line.HasSwitch(sw))
      return;
    *out = command_line.GetSwitchValueASCII(sw);
    applied = true;
  };

  take_string(kStealthUaSwitch, &user_agent_);
  take_string(kStealthPlatformSwitch, &platform_);
  take_string(kStealthLanguageSwitch, &language_);
  take_string(kStealthWebglVendorSwitch, &webgl_vendor_);
  take_string(kStealthWebglRendererSwitch, &webgl_renderer_);

  if (command_line.HasSwitch(kStealthLanguagesSwitch)) {
    std::vector<std::string> out = base::SplitString(
        command_line.GetSwitchValueASCII(kStealthLanguagesSwitch), ",",
        base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
    if (!out.empty()) {
      languages_ = std::move(out);
      applied = true;
    }
  }

  if (command_line.HasSwitch(kStealthHardwareConcurrencySwitch)) {
    unsigned v = 0;
    if (base::StringToUint(
            command_line.GetSwitchValueASCII(kStealthHardwareConcurrencySwitch),
            &v)) {
      hw_concurrency_ = v;
      applied = true;
    }
  }

  if (command_line.HasSwitch(kStealthDeviceMemorySwitch)) {
    double v = 0;
    if (base::StringToDouble(
            command_line.GetSwitchValueASCII(kStealthDeviceMemorySwitch), &v)) {
      device_memory_ = static_cast<float>(v);
      applied = true;
    }
  }

  if (command_line.HasSwitch(kStealthMaxTouchPointsSwitch)) {
    int v = 0;
    if (base::StringToInt(
            command_line.GetSwitchValueASCII(kStealthMaxTouchPointsSwitch),
            &v)) {
      max_touch_points_ = v;
      applied = true;
    }
  }

  // --stealth-screen=WIDTHxHEIGHT[xDEPTH]. The optional third component sets the
  // color depth (a dedicated --stealth-color-depth below can still override it).
  if (command_line.HasSwitch(kStealthScreenSwitch)) {
    std::vector<std::string> parts = base::SplitString(
        command_line.GetSwitchValueASCII(kStealthScreenSwitch), "x",
        base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
    int w = 0, h = 0;
    if (parts.size() >= 2 && base::StringToInt(parts[0], &w) &&
        base::StringToInt(parts[1], &h)) {
      screen_width_ = w;
      screen_height_ = h;
      applied = true;
      unsigned depth = 0;
      if (parts.size() >= 3 && base::StringToUint(parts[2], &depth))
        color_depth_ = depth;
    }
  }

  if (command_line.HasSwitch(kStealthColorDepthSwitch)) {
    unsigned v = 0;
    if (base::StringToUint(
            command_line.GetSwitchValueASCII(kStealthColorDepthSwitch), &v)) {
      color_depth_ = v;
      applied = true;
    }
  }

  if (command_line.HasSwitch(kStealthSeedSwitch)) {
    noise_seed_ =
        HashSeed(command_line.GetSwitchValueASCII(kStealthSeedSwitch));
    applied = true;
  }

  if (applied)
    active_ = true;
  return applied;
}

}  // namespace stealth
