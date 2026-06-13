// Copyright 2026 The Stealth Browser Authors.

#include "stealth/src/fingerprint/seeded_noise.h"

#include "stealth/src/fingerprint/fingerprint_config.h"

namespace stealth {

namespace {

// FNV-1a over the domain tag. Mirrors FingerprintConfig's seed hash so the noise
// family shares one hashing convention; here it decorrelates surfaces that share
// the same base seed.
uint64_t HashDomain(std::string_view domain) {
  uint64_t h = 1469598103934665603ULL;  // FNV-1a offset basis.
  for (char c : domain) {
    h ^= static_cast<uint8_t>(c);
    h *= 1099511628211ULL;  // FNV prime.
  }
  return h;
}

// One splitmix64 step. Stateless w.r.t. anything but `state`; well-distributed
// even from a zero seed, so no special-casing of empty seeds is required.
uint64_t SplitMix64(uint64_t& state) {
  state += 0x9E3779B97F4A7C15ULL;  // golden-ratio increment.
  uint64_t z = state;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}

}  // namespace

// static
std::optional<SeededNoise> SeededNoise::ForSurface(std::string_view domain) {
  std::optional<uint64_t> seed = FingerprintConfig::Get().NoiseSeed();
  if (!seed)
    return std::nullopt;
  return SeededNoise(*seed, domain);
}

SeededNoise::SeededNoise(uint64_t base_seed, std::string_view domain)
    // Mix the per-surface domain into the base seed. XOR-then-mix via one
    // SplitMix64 step so adjacent domains ("canvas" vs "canvas2") produce
    // well-separated streams rather than near-identical ones.
    : state_(base_seed ^ HashDomain(domain)) {
  uint64_t s = state_;
  state_ = SplitMix64(s);
}

uint64_t SeededNoise::NextUint64() {
  return SplitMix64(state_);
}

double SeededNoise::NextDouble() {
  // Take the top 53 bits for a uniform double in [0, 1) (standard construction;
  // 2^-53 spacing, no bias).
  return (NextUint64() >> 11) * (1.0 / 9007199254740992.0);
}

double SeededNoise::NextJitter(double magnitude) {
  return (NextDouble() * 2.0 - 1.0) * magnitude;
}

int SeededNoise::NextIntDelta(int bound) {
  if (bound <= 0)
    return 0;
  // Span is [-bound, +bound] => 2*bound + 1 distinct values, unbiased modulo.
  const uint64_t span = static_cast<uint64_t>(2 * bound + 1);
  return static_cast<int>(NextUint64() % span) - bound;
}

}  // namespace stealth
