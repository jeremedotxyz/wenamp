#pragma once
#include <array>
#include <cstddef>
#include <vector>

#include "wenamp/fft.hpp"

namespace wenamp {

// Turns the most recent mono samples into 19 log-spaced bars (the classic
// 76 px / 4 px visualizer) with instant attack, smooth decay and falling peaks.
class SpectrumAnalyzer {
 public:
  static constexpr int kBars = 19;

  explicit SpectrumAnalyzer(unsigned sampleRate = 44100, std::size_t fftSize = 1024);

  // Analyze up to fftSize trailing samples (zero-padded when shorter).
  // dt is the elapsed time in seconds since the previous call (drives decay).
  void process(const float* mono, std::size_t n, float dt);
  // No new audio: let bars and peaks fall.
  void decay(float dt);
  void reset();

  const std::array<float, kBars>& bars() const { return bars_; }    // 0..1
  const std::array<float, kBars>& peaks() const { return peaks_; }  // 0..1

  // Inclusive FFT bin range for a bar (exposed for tests / docs).
  std::pair<std::size_t, std::size_t> binRange(int bar) const { return ranges_[bar]; }
  static float dbFloor() { return -70.0f; }

 private:
  void settle(const std::array<float, kBars>& target, float dt);

  unsigned sr_;
  std::size_t n_;
  FFT fft_;
  std::vector<float> window_;
  std::vector<std::complex<float>> buf_;
  std::array<std::pair<std::size_t, std::size_t>, kBars> ranges_{};
  std::array<float, kBars> bars_{};
  std::array<float, kBars> peaks_{};
  std::array<float, kBars> peakVel_{};
};

}  // namespace wenamp
