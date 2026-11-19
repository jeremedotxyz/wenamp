#include "wenamp/eq.hpp"

#include <algorithm>
#include <cmath>
#include <complex>

namespace wenamp {

const std::array<float, kEqBands> kEqFreqs = {60, 170, 310, 600, 1000, 3000, 6000, 12000, 14000, 16000};
const std::array<const char*, kEqBands> kEqLabels = {"60", "170", "310", "600", "1K",
                                                     "3K", "6K",  "12K", "14K", "16K"};

const std::vector<EqPreset>& eqPresets() {
  static const std::vector<EqPreset> presets = {
      {"Flat", {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 0},
      {"Rock", {4, 3, 1, -2, -3, -1, 2, 4, 5, 5}, -3},
      {"Pop", {-1, 2, 4, 5, 3, 0, -1, -1, 1, 1}, -2},
      {"Jazz", {3, 2, 1, 2, -2, -2, 0, 1, 2, 3}, -2},
      {"Classical", {3, 2, 1, 0, 0, 0, -1, -2, -2, -3}, -1},
      {"Bass Boost", {7, 6, 4, 2, 0, 0, 0, 0, 0, 0}, -5},
      {"Treble Boost", {0, 0, 0, 0, 0, 1, 3, 5, 6, 7}, -4},
      {"Vocal", {-3, -2, 0, 3, 5, 4, 2, 0, -1, -2}, -2},
  };
  return presets;
}

namespace {
constexpr double kQ = 1.2;
constexpr double kPi = 3.14159265358979323846;
}  // namespace

Equalizer::Equalizer(unsigned sampleRate) : sr_(sampleRate) {
  gains_.fill(0.0f);
  for (int i = 0; i < kEqBands; ++i) recompute(i);
}

void Equalizer::recompute(int band) {
  Biquad& q = bq_[band];
  const double db = gains_[band];
  if (db == 0.0) {
    q = Biquad{};
    return;
  }
  const double A = std::pow(10.0, db / 40.0);
  const double w0 = 2.0 * kPi * kEqFreqs[band] / static_cast<double>(sr_);
  const double alpha = std::sin(w0) / (2.0 * kQ);
  const double cw = std::cos(w0);
  const double a0 = 1.0 + alpha / A;
  q.b0 = (1.0 + alpha * A) / a0;
  q.b1 = (-2.0 * cw) / a0;
  q.b2 = (1.0 - alpha * A) / a0;
  q.a1 = (-2.0 * cw) / a0;
  q.a2 = (1.0 - alpha / A) / a0;
  q.z1[0] = q.z1[1] = q.z2[0] = q.z2[1] = 0.0;
}

void Equalizer::setGain(int band, float db) {
  if (band < 0 || band >= kEqBands) return;
  db = std::clamp(db, -kEqMaxDb, kEqMaxDb);
  if (gains_[band] == db) return;
  gains_[band] = db;
  recompute(band);
}

void Equalizer::setPreamp(float db) {
  preampDb_ = std::clamp(db, -kEqMaxDb, kEqMaxDb);
  preampLin_ = std::pow(10.0f, preampDb_ / 20.0f);
}

void Equalizer::applyPreset(const EqPreset& p) {
  for (int i = 0; i < kEqBands; ++i) setGain(i, p.gains[i]);
  setPreamp(p.preamp);
}

void Equalizer::reset() {
  for (auto& q : bq_) q.z1[0] = q.z1[1] = q.z2[0] = q.z2[1] = 0.0;
}

void Equalizer::process(float* io, std::size_t frames) {
  if (!enabled_) return;
  const bool anyBand = std::any_of(gains_.begin(), gains_.end(), [](float g) { return g != 0.0f; });
  if (!anyBand && preampDb_ == 0.0f) return;

  for (std::size_t f = 0; f < frames; ++f) {
    for (int ch = 0; ch < 2; ++ch) {
      double x = static_cast<double>(io[f * 2 + ch]) * preampLin_;
      for (int b = 0; b < kEqBands; ++b) {
        if (gains_[b] == 0.0f) continue;
        Biquad& q = bq_[b];
        const double y = q.b0 * x + q.z1[ch];  // transposed direct form II
        q.z1[ch] = q.b1 * x - q.a1 * y + q.z2[ch];
        q.z2[ch] = q.b2 * x - q.a2 * y;
        x = y;
      }
      io[f * 2 + ch] = static_cast<float>(x);
    }
  }
}

std::vector<float> Equalizer::responseDb(const std::vector<float>& freqs) const {
  std::vector<float> out;
  out.reserve(freqs.size());
  for (float hz : freqs) {
    const double w = 2.0 * kPi * hz / static_cast<double>(sr_);
    const std::complex<double> z1 = std::polar(1.0, -w);
    const std::complex<double> z2 = std::polar(1.0, -2.0 * w);
    double db = preampDb_;
    for (int b = 0; b < kEqBands; ++b) {
      if (gains_[b] == 0.0f) continue;
      const Biquad& q = bq_[b];
      const std::complex<double> num = q.b0 + q.b1 * z1 + q.b2 * z2;
      const std::complex<double> den = 1.0 + q.a1 * z1 + q.a2 * z2;
      db += 20.0 * std::log10(std::abs(num / den));
    }
    out.push_back(static_cast<float>(db));
  }
  return out;
}

}  // namespace wenamp
