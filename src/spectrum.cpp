#include "wenamp/spectrum.hpp"

#include <algorithm>
#include <cmath>

namespace wenamp {

namespace {
constexpr float kLowHz = 50.0f;
constexpr float kHighHz = 16000.0f;
constexpr float kFallPerSec = 1.6f;   // bar decay (full scale per second)
constexpr float kPeakGravity = 1.8f;  // peak acceleration (scale / s^2)
}  // namespace

SpectrumAnalyzer::SpectrumAnalyzer(unsigned sampleRate, std::size_t fftSize)
    : sr_(sampleRate), n_(fftSize), fft_(fftSize), window_(fftSize), buf_(fftSize) {
  const double pi = std::acos(-1.0);
  for (std::size_t i = 0; i < n_; ++i)
    window_[i] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * pi * static_cast<double>(i) /
                                                         static_cast<double>(n_ - 1)));

  const double binHz = static_cast<double>(sr_) / static_cast<double>(n_);
  const double ratio = std::pow(static_cast<double>(kHighHz / kLowHz), 1.0 / kBars);
  for (int b = 0; b < kBars; ++b) {
    const double lo = kLowHz * std::pow(ratio, b);
    const double hi = kLowHz * std::pow(ratio, b + 1);
    std::size_t a = static_cast<std::size_t>(std::floor(lo / binHz));
    std::size_t z = static_cast<std::size_t>(std::ceil(hi / binHz)) - 1;
    a = std::max<std::size_t>(a, 1);
    z = std::max(z, a);
    z = std::min(z, n_ / 2 - 1);
    a = std::min(a, z);
    ranges_[b] = {a, z};
  }
  reset();
}

void SpectrumAnalyzer::reset() {
  bars_.fill(0.0f);
  peaks_.fill(0.0f);
  peakVel_.fill(0.0f);
}

void SpectrumAnalyzer::process(const float* mono, std::size_t n, float dt) {
  const std::size_t take = std::min(n, n_);
  const float* src = mono + (n - take);
  for (std::size_t i = 0; i < n_; ++i) {
    const float s = i < take ? src[i] : 0.0f;
    buf_[i] = {s * window_[i], 0.0f};
  }
  fft_.forward(buf_);

  // A full-scale sine under a Hann window peaks at amplitude * N / 4.
  const float norm = 4.0f / static_cast<float>(n_);
  std::array<float, kBars> target{};
  for (int b = 0; b < kBars; ++b) {
    float m = 0.0f;
    for (std::size_t k = ranges_[b].first; k <= ranges_[b].second; ++k)
      m = std::max(m, std::abs(buf_[k]));
    const float db = 20.0f * std::log10(std::max(m * norm, 1e-9f));
    float v = (db - dbFloor()) / (0.0f - dbFloor());
    // Mild tilt so the high bars are not permanently starved (pink-ish music).
    v += 0.012f * static_cast<float>(b);
    target[b] = std::clamp(v, 0.0f, 1.0f);
  }
  settle(target, dt);
}

void SpectrumAnalyzer::decay(float dt) {
  std::array<float, kBars> zero{};
  settle(zero, dt);
}

void SpectrumAnalyzer::settle(const std::array<float, kBars>& target, float dt) {
  for (int b = 0; b < kBars; ++b) {
    bars_[b] = target[b] >= bars_[b] ? target[b] : std::max(target[b], bars_[b] - kFallPerSec * dt);
    if (bars_[b] >= peaks_[b]) {
      peaks_[b] = bars_[b];
      peakVel_[b] = 0.0f;
    } else {
      peakVel_[b] += kPeakGravity * dt;
      peaks_[b] = std::max(bars_[b], peaks_[b] - peakVel_[b] * dt);
    }
  }
}

}  // namespace wenamp
