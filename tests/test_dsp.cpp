#include <algorithm>
#include <cmath>
#include <complex>
#include <stdexcept>
#include <vector>

#include "testing.hpp"
#include "wenamp/eq.hpp"
#include "wenamp/fft.hpp"
#include "wenamp/spectrum.hpp"
#include "wenamp/wav.hpp"

using namespace wenamp;

namespace {
std::vector<float> monoTone(double hz, std::size_t n, double amp = 0.5) {
  std::vector<float> v(n);
  for (std::size_t i = 0; i < n; ++i) v[i] = static_cast<float>(amp * std::sin(2.0 * M_PI * hz * i / 44100.0));
  return v;
}
double rms(const std::vector<float>& v, std::size_t from) {
  double s = 0;
  for (std::size_t i = from; i < v.size(); ++i) s += static_cast<double>(v[i]) * v[i];
  return std::sqrt(s / static_cast<double>(v.size() - from));
}
}  // namespace

TEST(fft_impulse_is_flat) {
  FFT f(256);
  std::vector<std::complex<float>> d(256);
  d[0] = {1.0f, 0.0f};
  f.forward(d);
  for (const auto& c : d) CHECK_NEAR(std::abs(c), 1.0, 1e-5);
}

TEST(fft_sine_lands_in_its_bin) {
  FFT f(1024);
  std::vector<std::complex<float>> d(1024);
  for (int i = 0; i < 1024; ++i) d[i] = {std::sin(2.0f * 3.14159265f * 100.0f * i / 1024.0f), 0.0f};
  f.forward(d);
  int peak = 1;
  for (int i = 1; i < 512; ++i)
    if (std::abs(d[i]) > std::abs(d[peak])) peak = i;
  CHECK_EQ(peak, 100);
  CHECK_NEAR(std::abs(d[100]), 512.0, 0.5);  // amplitude * N / 2
}

TEST(fft_matches_naive_dft) {
  const int n = 64;
  FFT f(n);
  std::vector<std::complex<float>> d(n);
  for (int i = 0; i < n; ++i) d[i] = {std::sin(0.3f * i) + 0.5f * std::cos(1.7f * i), 0.0f};
  std::vector<std::complex<double>> ref(n);
  for (int k = 0; k < n; ++k)
    for (int t = 0; t < n; ++t)
      ref[k] += static_cast<double>(d[t].real()) * std::polar(1.0, -2.0 * M_PI * k * t / n);
  f.forward(d);
  double worst = 0;
  for (int k = 0; k < n; ++k) worst = std::max(worst, std::abs(std::complex<double>(d[k]) - ref[k]));
  CHECK(worst < 1e-3);
}

TEST(fft_rejects_non_power_of_two) {
  bool threw = false;
  try {
    FFT f(1000);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  CHECK(threw);
}

TEST(spectrum_silence_is_zero) {
  SpectrumAnalyzer a;
  std::vector<float> z(1024, 0.0f);
  a.process(z.data(), z.size(), 1.0f / 60.0f);
  for (float b : a.bars()) CHECK(b < 0.05f);
}

TEST(spectrum_tone_lights_the_right_bar) {
  SpectrumAnalyzer a;
  const auto tone = monoTone(1000.0, 1024);
  a.process(tone.data(), tone.size(), 1.0f / 60.0f);
  int best = 0;
  for (int b = 1; b < SpectrumAnalyzer::kBars; ++b)
    if (a.bars()[b] > a.bars()[best]) best = b;
  const std::size_t bin = static_cast<std::size_t>(std::lround(1000.0 / (44100.0 / 1024.0)));
  const auto r = a.binRange(best);
  CHECK(bin >= r.first && bin <= r.second);
  CHECK(a.bars()[best] > 0.8f);
}

TEST(spectrum_bar_ranges_are_ordered_and_cover_the_band) {
  SpectrumAnalyzer a;
  for (int b = 0; b < SpectrumAnalyzer::kBars; ++b) {
    const auto r = a.binRange(b);
    CHECK(r.first <= r.second);
    if (b > 0) CHECK(r.first >= a.binRange(b - 1).first);
  }
  CHECK(a.binRange(SpectrumAnalyzer::kBars - 1).second <= 511);
}

TEST(spectrum_bars_fall_and_peaks_hang) {
  SpectrumAnalyzer a;
  const auto tone = monoTone(440.0, 1024, 0.9);
  a.process(tone.data(), tone.size(), 1.0f / 60.0f);
  const float loud = *std::max_element(a.bars().begin(), a.bars().end());
  a.decay(0.1f);
  const float after = *std::max_element(a.bars().begin(), a.bars().end());
  CHECK(after < loud);
  const float peak = *std::max_element(a.peaks().begin(), a.peaks().end());
  CHECK(peak >= after);
  a.decay(2.0f);
  CHECK(*std::max_element(a.bars().begin(), a.bars().end()) < 0.02f);
}

TEST(eq_flat_is_a_bit_exact_bypass) {
  Equalizer eq;
  auto a = makeTone(1000.0, 0.2);
  const auto b = a;
  eq.process(a.data(), a.size() / 2);
  CHECK(a == b);
}

TEST(eq_boost_raises_the_band) {
  Equalizer eq;
  eq.setGain(4, 12.0f);  // 1 kHz
  const auto dry = makeTone(1000.0, 1.0, 44100, 0.1);
  auto wet = dry;
  eq.process(wet.data(), wet.size() / 2);
  const double db = 20.0 * std::log10(rms(wet, 22050) / rms(dry, 22050));
  CHECK_NEAR(db, 12.0, 0.6);
}

TEST(eq_cut_lowers_the_band_and_leaves_distant_bands_alone) {
  Equalizer eq;
  eq.setGain(4, -12.0f);
  const auto dry = makeTone(1000.0, 1.0, 44100, 0.2);
  auto wet = dry;
  eq.process(wet.data(), wet.size() / 2);
  CHECK_NEAR(20.0 * std::log10(rms(wet, 22050) / rms(dry, 22050)), -12.0, 0.6);

  Equalizer eq2;
  eq2.setGain(4, 12.0f);
  const auto far = makeTone(80.0, 1.0, 44100, 0.2);  // two octaves+ below
  auto farWet = far;
  eq2.process(farWet.data(), farWet.size() / 2);
  CHECK_NEAR(20.0 * std::log10(rms(farWet, 22050) / rms(far, 22050)), 0.0, 1.0);
}

TEST(eq_response_matches_the_setting) {
  Equalizer eq;
  eq.setGain(4, 6.0f);
  const auto r = eq.responseDb({1000.0f, 60.0f, 16000.0f});
  CHECK_NEAR(r[0], 6.0, 0.1);
  CHECK(std::fabs(r[1]) < 1.0f);
  CHECK(std::fabs(r[2]) < 1.0f);
}

TEST(eq_preamp_scales_the_signal) {
  Equalizer eq;
  eq.setPreamp(-6.0f);
  auto s = makeTone(500.0, 0.1, 44100, 0.5);
  const auto dry = s;
  eq.process(s.data(), s.size() / 2);
  CHECK_NEAR(rms(s, 100) / rms(dry, 100), std::pow(10.0, -6.0 / 20.0), 0.01);
}

TEST(eq_disabled_leaves_the_signal_untouched) {
  Equalizer eq;
  eq.setGain(2, 12.0f);
  eq.setEnabled(false);
  auto a = makeTone(310.0, 0.1);
  const auto b = a;
  eq.process(a.data(), a.size() / 2);
  CHECK(a == b);
}

TEST(eq_clamps_gain_and_presets_are_sane) {
  Equalizer eq;
  eq.setGain(0, 99.0f);
  eq.setGain(1, -99.0f);
  CHECK_NEAR(eq.gain(0), kEqMaxDb, 1e-6);
  CHECK_NEAR(eq.gain(1), -kEqMaxDb, 1e-6);
  const auto& ps = eqPresets();
  CHECK(ps.size() >= 5);
  for (float g : ps[0].gains) CHECK_NEAR(g, 0.0, 1e-6);  // Flat comes first
  for (const auto& p : ps) {
    for (float g : p.gains) CHECK(std::fabs(g) <= kEqMaxDb);
    CHECK(std::fabs(p.preamp) <= kEqMaxDb);
  }
}
