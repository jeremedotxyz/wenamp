#include "wenamp/player.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>

#include "miniaudio.h"
#include "wenamp/version.hpp"

namespace wenamp {

struct Player::Impl {
  ma_decoder dec{};
  bool decOpen = false;
  ma_device dev{};
  bool devInit = false;
};

namespace {
void deviceCallback(ma_device* d, void* out, const void*, ma_uint32 frames) {
  static_cast<Player*>(d->pUserData)->render(static_cast<float*>(out), frames);
}
}  // namespace

Player::Player() : impl_(new Impl), ring_(new float[kRing]()), eq_(kSampleRate) {}

Player::~Player() {
  stopDevice();
  close();
}

bool Player::open(const std::string& path, std::string* err) {
  // Probe the native format first (a decoder configured with unknown/0 reports
  // the file's own rate and channel count), then reopen resampled to 44.1k f32.
  ma_decoder probe;
  ma_decoder_config pc = ma_decoder_config_init_default();
  if (ma_decoder_init_file(path.c_str(), &pc, &probe) != MA_SUCCESS) {
    if (err) *err = "cannot decode " + path;
    return false;
  }
  ma_format fmt;
  ma_uint32 srcCh = 0, srcRate = 0;
  ma_decoder_get_data_format(&probe, &fmt, &srcCh, &srcRate, nullptr, 0);
  ma_uint64 nativeLen = 0;
  ma_decoder_get_length_in_pcm_frames(&probe, &nativeLen);
  ma_decoder_uninit(&probe);

  ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, kChannels, kSampleRate);
  std::lock_guard<std::mutex> lk(mu_);
  if (impl_->decOpen) {
    ma_decoder_uninit(&impl_->dec);
    impl_->decOpen = false;
  }
  if (ma_decoder_init_file(path.c_str(), &cfg, &impl_->dec) != MA_SUCCESS) {
    hasTrack_ = false;
    state_ = PlayState::Stopped;
    if (err) *err = "cannot decode " + path;
    return false;
  }
  impl_->decOpen = true;

  ma_uint64 len = 0;
  ma_decoder_get_length_in_pcm_frames(&impl_->dec, &len);
  totalFrames_ = len;
  framesPlayed_ = 0;
  eq_.reset();
  ended_ = false;

  info_ = TrackInfo{};
  info_.path = path;
  info_.sourceRate = srcRate;
  info_.sourceChannels = srcCh;
  info_.duration = srcRate ? static_cast<double>(nativeLen) / srcRate : 0.0;
  std::error_code ec;
  const auto bytes = std::filesystem::file_size(path, ec);
  if (!ec && info_.duration > 0.5)
    info_.bitrateKbps = static_cast<unsigned>(std::lround(static_cast<double>(bytes) * 8.0 / info_.duration / 1000.0));

  hasTrack_ = true;
  state_ = PlayState::Stopped;
  return true;
}

void Player::close() {
  std::lock_guard<std::mutex> lk(mu_);
  if (impl_->decOpen) {
    ma_decoder_uninit(&impl_->dec);
    impl_->decOpen = false;
  }
  hasTrack_ = false;
  state_ = PlayState::Stopped;
  framesPlayed_ = totalFrames_ = 0;
  info_ = TrackInfo{};
}

void Player::play() {
  std::lock_guard<std::mutex> lk(mu_);
  if (!impl_->decOpen) return;
  if (state_ == PlayState::Stopped && totalFrames_ > 0 && framesPlayed_ >= totalFrames_) {
    ma_decoder_seek_to_pcm_frame(&impl_->dec, 0);
    framesPlayed_ = 0;
    eq_.reset();
  }
  ended_ = false;
  state_ = PlayState::Playing;
}

void Player::pause() {
  if (state_ == PlayState::Playing) state_ = PlayState::Paused;
}

void Player::togglePause() {
  if (state_ == PlayState::Playing) state_ = PlayState::Paused;
  else if (state_ == PlayState::Paused) state_ = PlayState::Playing;
}

void Player::stop() {
  std::lock_guard<std::mutex> lk(mu_);
  state_ = PlayState::Stopped;
  if (impl_->decOpen) {
    ma_decoder_seek_to_pcm_frame(&impl_->dec, 0);
    framesPlayed_ = 0;
    eq_.reset();
  }
}

void Player::seek(double seconds) {
  std::lock_guard<std::mutex> lk(mu_);
  if (!impl_->decOpen) return;
  const double maxSec = totalFrames_ ? static_cast<double>(totalFrames_) / kSampleRate : info_.duration;
  const double s = std::clamp(seconds, 0.0, std::max(0.0, maxSec));
  std::uint64_t frame = static_cast<std::uint64_t>(s * kSampleRate);
  if (totalFrames_ && frame >= totalFrames_) frame = totalFrames_ > 0 ? totalFrames_ - 1 : 0;
  if (ma_decoder_seek_to_pcm_frame(&impl_->dec, frame) == MA_SUCCESS) {
    framesPlayed_ = frame;
    eq_.reset();
  }
}

double Player::position() const {
  std::lock_guard<std::mutex> lk(mu_);
  return static_cast<double>(framesPlayed_) / kSampleRate;
}

double Player::duration() const {
  std::lock_guard<std::mutex> lk(mu_);
  return info_.duration;
}

TrackInfo Player::info() const {
  std::lock_guard<std::mutex> lk(mu_);
  return info_;
}

void Player::setVolume(float v) { volume_ = std::clamp(v, 0.0f, 1.0f); }
void Player::setBalance(float b) { balance_ = std::clamp(b, -1.0f, 1.0f); }

void Player::setEqEnabled(bool on) {
  std::lock_guard<std::mutex> lk(mu_);
  eq_.setEnabled(on);
}
bool Player::eqEnabled() const {
  std::lock_guard<std::mutex> lk(mu_);
  return eq_.enabled();
}
void Player::setEqGain(int band, float db) {
  std::lock_guard<std::mutex> lk(mu_);
  eq_.setGain(band, db);
}
float Player::eqGain(int band) const {
  std::lock_guard<std::mutex> lk(mu_);
  return eq_.gain(band);
}
void Player::setEqPreamp(float db) {
  std::lock_guard<std::mutex> lk(mu_);
  eq_.setPreamp(db);
}
float Player::eqPreamp() const {
  std::lock_guard<std::mutex> lk(mu_);
  return eq_.preamp();
}
void Player::applyEqPreset(const EqPreset& p) {
  std::lock_guard<std::mutex> lk(mu_);
  eq_.applyPreset(p);
}
std::vector<float> Player::eqResponse(const std::vector<float>& freqs) const {
  std::lock_guard<std::mutex> lk(mu_);
  return eq_.responseDb(freqs);
}

void Player::render(float* out, std::size_t frames) {
  std::size_t produced = 0;
  {
    std::lock_guard<std::mutex> lk(mu_);
    if (state_ == PlayState::Playing && impl_->decOpen) {
      ma_uint64 read = 0;
      ma_decoder_read_pcm_frames(&impl_->dec, out, frames, &read);
      produced = static_cast<std::size_t>(read);
      framesPlayed_ += read;
      if (produced < frames) {
        state_ = PlayState::Stopped;
        ended_ = true;
        if (totalFrames_) framesPlayed_ = totalFrames_;
      }
      if (produced) eq_.process(out, produced);
    }
  }
  if (produced < frames) std::memset(out + produced * 2, 0, (frames - produced) * 2 * sizeof(float));

  const float vol = volume_.load();
  const float bal = balance_.load();
  const float gl = vol * (bal > 0.0f ? 1.0f - bal : 1.0f);
  const float gr = vol * (bal < 0.0f ? 1.0f + bal : 1.0f);
  std::size_t pos = ringPos_.load(std::memory_order_relaxed);
  for (std::size_t i = 0; i < frames; ++i) {
    const float l = std::clamp(out[i * 2] * gl, -1.0f, 1.0f);
    const float r = std::clamp(out[i * 2 + 1] * gr, -1.0f, 1.0f);
    out[i * 2] = l;
    out[i * 2 + 1] = r;
    ring_[(pos + i) % kRing] = 0.5f * (l + r);
  }
  ringPos_.store(pos + frames, std::memory_order_release);
}

void Player::copyLatest(float* dst, std::size_t n) const {
  n = std::min(n, kRing);
  const std::size_t pos = ringPos_.load(std::memory_order_acquire);
  for (std::size_t i = 0; i < n; ++i) dst[i] = ring_[(pos + kRing - n + i) % kRing];
}

bool Player::startDevice(std::string* err) {
  if (deviceRunning_) return true;
  ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
  cfg.playback.format = ma_format_f32;
  cfg.playback.channels = kChannels;
  cfg.sampleRate = kSampleRate;
  cfg.dataCallback = deviceCallback;
  cfg.pUserData = this;
  if (ma_device_init(nullptr, &cfg, &impl_->dev) != MA_SUCCESS) {
    if (err) *err = "no audio output device available";
    return false;
  }
  impl_->devInit = true;
  if (ma_device_start(&impl_->dev) != MA_SUCCESS) {
    ma_device_uninit(&impl_->dev);
    impl_->devInit = false;
    if (err) *err = "could not start the audio device";
    return false;
  }
  deviceRunning_ = true;
  return true;
}

void Player::stopDevice() {
  if (impl_->devInit) {
    ma_device_uninit(&impl_->dev);
    impl_->devInit = false;
  }
  deviceRunning_ = false;
}

}  // namespace wenamp
