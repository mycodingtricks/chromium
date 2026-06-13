// Copyright 2026 The Stealth Browser Authors.
//
// Single, deterministic derivation point for every seeded-noise surface
// (canvas, audio, WebGL readback, DOMRect, SVG metrics, ...). The orchestrator
// supplies ONE noise seed per launch (profile "seed" / --stealth-seed), which
// FingerprintConfig hashes into a u64. This class turns that one seed into a
// per-surface, deterministic, stateless PRNG stream so that:
//
//   - same seed in  => same hash out (stable across launches for an identity);
//   - different seed => different noise (distinct across identities);
//   - two surfaces never invent their own entropy — they all branch off the
//     single profile seed via a stable per-surface domain tag.
//
// This is the "derive everything from one profile source" rule made concrete
// for the noise family. Surfaces MUST obtain their stream via ForSurface()
// (which returns std::nullopt when the profile supplied no seed, so the surface
// falls through to stock Chromium behaviour) rather than constructing their own
// generator from any other entropy.
//
// Keep this Blink-free (no WTF types) so it can be depended on from any layer,
// exactly like FingerprintConfig.

#ifndef STEALTH_SRC_FINGERPRINT_SEEDED_NOISE_H_
#define STEALTH_SRC_FINGERPRINT_SEEDED_NOISE_H_

#include <cstdint>
#include <optional>
#include <string_view>

namespace stealth {

// A deterministic per-surface noise stream. Construction is cheap; copies are
// independent streams (copying snapshots the current state). Not thread-safe;
// construct one per use site.
class SeededNoise {
 public:
  // Build a stream for `domain` (a stable surface tag, e.g. "canvas",
  // "audio", "webgl.readpixels", "domrect") from the active profile's noise
  // seed. Returns std::nullopt when no seed was supplied, signalling the caller
  // to leave the surface untouched (stock behaviour). This is the only sanctioned
  // entry point for noise surfaces.
  static std::optional<SeededNoise> ForSurface(std::string_view domain);

  // Build a stream directly from an explicit base seed. Useful for tests and for
  // callers that already hold the seed. `domain` decorrelates surfaces that
  // share a seed so their hashes move independently.
  SeededNoise(uint64_t base_seed, std::string_view domain);

  // Next 64-bit value of the stream (splitmix64 — fast, full-period, passes
  // BigCrush; deterministic for a given (seed, domain)).
  uint64_t NextUint64();

  // Uniform double in [0, 1).
  double NextDouble();

  // Signed jitter uniformly in [-magnitude, +magnitude]. The workhorse for
  // sub-pixel / sub-sample perturbation that stays visually/aurally identical.
  double NextJitter(double magnitude);

  // Small signed integer delta in [-bound, +bound] (inclusive). For perturbing
  // 8-bit pixel channels etc. `bound` is clamped to >= 0.
  int NextIntDelta(int bound);

 private:
  uint64_t state_;
};

}  // namespace stealth

#endif  // STEALTH_SRC_FINGERPRINT_SEEDED_NOISE_H_
