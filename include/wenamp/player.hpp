#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include "wenamp/eq.hpp"

namespace wenamp {

enum class PlayState { Stopped, Playing, Paused };

struct TrackInfo {
  std::string path;
  double duration = 0.0;       // seconds
  unsigned sourceRate = 0;     // Hz of the file before resampling
  unsigned sourceChannels = 0;
  unsigned bitrateKbps = 0;    // average, from file size / duration
};

// The audio engine: decode -> preamp/EQ -> volume/balance -> output, with a
// lock-free tap feeding the visualizer. render() is the single pull point; the
// device callback and offline tools (tests, `analyze`, `render-ui`) share it.
class Player {
 public:
  Player();
  ~Player();
  Player(const Player&) = delete;
  Player& operator=(const Player&) = delete;

  // Transport
  bool open(const std::string& path, std::string* err = nullptr);
  void close();
  void play();
  void pause();
  void togglePause();
  void stop();
  void seek(double seconds);

  // State
  PlayState state() const { return state_.load(); }
  bool hasTrack() const { return hasTrack_.load(); }
  double position() const;
  double duration() const;
  TrackInfo info() const;
  // True once after a track plays to its end; cleared by the call.
  bool consumeEnded() { return ended_.exchange(false); }

  // Mixer
  void setVolume(float v);   // 0..1
  float volume() const { return volume_.load(); }
  void setBalance(float b);  // -1 (left) .. +1 (right)
  float balance() const { return balance_.load(); }

  // Equalizer (thread-safe wrappers)
  void setEqEnabled(bool on);
  bool eqEnabled() const;
  void setEqGain(int band, float db);
  float eqGain(int band) const;
  void setEqPreamp(float db);
  float eqPreamp() const;
  void applyEqPreset(const EqPreset& p);
  std::vector<float> eqResponse(const std::vector<float>& freqs) const;

  // Pull `frames` stereo frames (interleaved float) at 44.1 kHz.
  void render(float* out, std::size_t frames);

  // Output device. With no audio hardware miniaudio falls back to its null
  // backend, so this still "plays" (silently) on CI machines.
  bool startDevice(std::string* err = nullptr);
  void stopDevice();
  bool deviceRunning() const { return deviceRunning_.load(); }

  // Copy the latest `n` mono samples (post-DSP) for the visualizer.
  void copyLatest(float* dst, std::size_t n) const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;

  mutable std::mutex mu_;  // guards decoder, position, EQ
  std::atomic<PlayState> state_{PlayState::Stopped};
  std::atomic<bool> hasTrack_{false};
  std::atomic<bool> ended_{false};
  std::atomic<bool> deviceRunning_{false};
  std::atomic<float> volume_{0.8f};
  std::atomic<float> balance_{0.0f};

  // visualizer tap
  static constexpr std::size_t kRing = 8192;
  std::unique_ptr<float[]> ring_;
  std::atomic<std::size_t> ringPos_{0};

  std::uint64_t framesPlayed_ = 0;
  std::uint64_t totalFrames_ = 0;
  TrackInfo info_;
  Equalizer eq_;
};

}  // namespace wenamp
