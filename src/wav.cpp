#include "wenamp/wav.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>

namespace wenamp {

namespace {
void put32(std::ofstream& f, std::uint32_t v) {
  const char b[4] = {char(v & 0xFF), char((v >> 8) & 0xFF), char((v >> 16) & 0xFF), char((v >> 24) & 0xFF)};
  f.write(b, 4);
}
void put16(std::ofstream& f, std::uint16_t v) {
  const char b[2] = {char(v & 0xFF), char((v >> 8) & 0xFF)};
  f.write(b, 2);
}
constexpr double kPi = 3.14159265358979323846;
}  // namespace

bool writeWav16(const std::string& path, const std::vector<float>& s, unsigned channels,
                unsigned sampleRate) {
  std::ofstream f(path, std::ios::binary);
  if (!f) return false;
  const std::uint32_t dataBytes = static_cast<std::uint32_t>(s.size() * 2);
  f.write("RIFF", 4);
  put32(f, 36 + dataBytes);
  f.write("WAVEfmt ", 8);
  put32(f, 16);
  put16(f, 1);  // PCM
  put16(f, static_cast<std::uint16_t>(channels));
  put32(f, sampleRate);
  put32(f, sampleRate * channels * 2);
  put16(f, static_cast<std::uint16_t>(channels * 2));
  put16(f, 16);
  f.write("data", 4);
  put32(f, dataBytes);
  for (float v : s) {
    const float c = std::clamp(v, -1.0f, 1.0f);
    put16(f, static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(c * 32767.0f))));
  }
  return static_cast<bool>(f);
}

std::vector<float> makeTone(double hz, double seconds, unsigned sr, double amp, double rightRatio) {
  const std::size_t frames = static_cast<std::size_t>(seconds * sr);
  std::vector<float> out(frames * 2);
  for (std::size_t i = 0; i < frames; ++i) {
    const double t = static_cast<double>(i) / sr;
    out[i * 2] = static_cast<float>(amp * std::sin(2.0 * kPi * hz * t));
    out[i * 2 + 1] = static_cast<float>(amp * std::sin(2.0 * kPi * hz * rightRatio * t));
  }
  return out;
}

std::vector<float> makeDemoMusic(double seconds, unsigned sr) {
  const std::size_t frames = static_cast<std::size_t>(seconds * sr);
  std::vector<float> out(frames * 2, 0.0f);
  // A minor 9 stack with decaying harmonics, a slow tremolo and a bass pulse.
  const double notes[] = {110.0, 164.81, 220.0, 261.63, 329.63, 493.88};
  for (std::size_t i = 0; i < frames; ++i) {
    const double t = static_cast<double>(i) / sr;
    double l = 0, r = 0;
    int n = 0;
    for (double f : notes) {
      for (int h = 1; h <= 6; ++h) {
        const double a = 0.10 / (h * h) * (1.0 + 0.5 * std::sin(2.0 * kPi * (0.25 + 0.07 * n) * t));
        const double v = a * std::sin(2.0 * kPi * f * h * t + n);
        (n % 2 ? l : r) += v * 0.6;
        (n % 2 ? r : l) += v * 0.4;
      }
      ++n;
    }
    const double beat = std::pow(std::max(0.0, std::sin(2.0 * kPi * 2.0 * t)), 8.0);
    const double kick = 0.45 * beat * std::sin(2.0 * kPi * (48.0 + 40.0 * beat) * t);
    const double hat = 0.05 * std::pow(std::max(0.0, std::sin(2.0 * kPi * 4.0 * t + 1.0)), 12.0) *
                       std::sin(2.0 * kPi * 7200.0 * t);
    out[i * 2] = static_cast<float>(std::clamp(l + kick + hat, -0.95, 0.95));
    out[i * 2 + 1] = static_cast<float>(std::clamp(r + kick + hat, -0.95, 0.95));
  }
  return out;
}

}  // namespace wenamp
