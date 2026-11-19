#pragma once
#include <array>
#include <cstddef>
#include <vector>

namespace wenamp {

constexpr int kEqBands = 10;
constexpr float kEqMaxDb = 12.0f;

// Centre frequencies (Hz) of the ten bands.
extern const std::array<float, kEqBands> kEqFreqs;
extern const std::array<const char*, kEqBands> kEqLabels;

struct EqPreset {
  const char* name;
  std::array<float, kEqBands> gains;  // dB per band
  float preamp;                       // dB
};
const std::vector<EqPreset>& eqPresets();

// Ten peaking biquads (RBJ cookbook) in series, plus a preamp. Stereo
// interleaved in-place processing. Bands at exactly 0 dB are skipped, and a
// fully flat EQ with 0 dB preamp is a bit-exact bypass.
class Equalizer {
 public:
  explicit Equalizer(unsigned sampleRate = 44100);

  void setGain(int band, float db);  // clamped to +/- kEqMaxDb
  float gain(int band) const { return gains_[band]; }
  void setPreamp(float db);
  float preamp() const { return preampDb_; }
  void setEnabled(bool on) { enabled_ = on; }
  bool enabled() const { return enabled_; }
  void applyPreset(const EqPreset& p);
  void reset();  // clear filter memory (call on seek / track change)

  void process(float* interleavedStereo, std::size_t frames);

  // Combined magnitude response in dB at the given frequencies (preamp included).
  std::vector<float> responseDb(const std::vector<float>& freqsHz) const;

 private:
  struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1[2] = {0, 0}, z2[2] = {0, 0};
  };
  void recompute(int band);

  unsigned sr_;
  bool enabled_ = true;
  float preampDb_ = 0.0f;
  float preampLin_ = 1.0f;
  std::array<float, kEqBands> gains_{};
  std::array<Biquad, kEqBands> bq_{};
};

}  // namespace wenamp
