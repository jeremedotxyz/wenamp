#include <algorithm>
#include <cmath>
#include <vector>

#include "testing.hpp"
#include "wenamp/player.hpp"
#include "wenamp/wav.hpp"

using namespace wenamp;

namespace {

std::string makeWav(const std::string& name, double hz, double seconds, double amp = 0.5) {
  const auto p = (testing::scratch() / name).string();
  writeWav16(p, makeTone(hz, seconds, 44100, amp), 2, 44100);
  return p;
}

struct Peaks {
  float l = 0, r = 0;
};
Peaks peaks(const std::vector<float>& out) {
  Peaks p;
  for (std::size_t i = 0; i + 1 < out.size(); i += 2) {
    p.l = std::max(p.l, std::fabs(out[i]));
    p.r = std::max(p.r, std::fabs(out[i + 1]));
  }
  return p;
}

}  // namespace

TEST(player_opens_and_reports_the_format) {
  Player p;
  const auto f = makeWav("one.wav", 440.0, 1.0);
  std::string err;
  CHECK(p.open(f, &err));
  CHECK(p.hasTrack());
  const TrackInfo i = p.info();
  CHECK_NEAR(i.duration, 1.0, 0.01);
  CHECK_EQ(i.sourceRate, 44100u);
  CHECK_EQ(i.sourceChannels, 2u);
  CHECK_NEAR(i.bitrateKbps, 1411.0, 20.0);
  CHECK(p.state() == PlayState::Stopped);
}

TEST(player_rejects_bad_files) {
  Player p;
  std::string err;
  CHECK(!p.open((testing::scratch() / "nope.wav").string(), &err));
  CHECK(!err.empty());
  CHECK(!p.hasTrack());
}

TEST(player_renders_audio_and_advances_position) {
  Player p;
  CHECK(p.open(makeWav("tone.wav", 440.0, 2.0)));
  p.play();
  std::vector<float> out(4410 * 2);
  p.render(out.data(), 4410);
  CHECK(p.state() == PlayState::Playing);
  CHECK_NEAR(p.position(), 0.1, 1e-4);
  const Peaks pk = peaks(out);
  CHECK(pk.l > 0.3f && pk.r > 0.3f);
}

TEST(player_is_silent_when_stopped_or_paused) {
  Player p;
  CHECK(p.open(makeWav("quiet.wav", 440.0, 1.0)));
  std::vector<float> out(1000 * 2, 1.0f);
  p.render(out.data(), 1000);  // stopped
  CHECK(peaks(out).l == 0.0f);
  p.play();
  p.render(out.data(), 1000);
  const double at = p.position();
  p.pause();
  CHECK(p.state() == PlayState::Paused);
  std::fill(out.begin(), out.end(), 1.0f);
  p.render(out.data(), 1000);
  CHECK(peaks(out).l == 0.0f);
  CHECK_NEAR(p.position(), at, 1e-9);
  p.togglePause();
  CHECK(p.state() == PlayState::Playing);
}

TEST(player_signals_the_end_of_a_track_once) {
  Player p;
  CHECK(p.open(makeWav("short.wav", 440.0, 0.5)));
  p.play();
  std::vector<float> out(4410 * 2);
  for (int i = 0; i < 8; ++i) p.render(out.data(), 4410);  // 0.8 s of pulls
  CHECK(p.state() == PlayState::Stopped);
  CHECK(p.consumeEnded());
  CHECK(!p.consumeEnded());
  CHECK_NEAR(p.position(), 0.5, 0.01);
  p.play();  // replays from the top
  CHECK(p.position() < 0.01);
  CHECK(p.state() == PlayState::Playing);
}

TEST(player_seek_and_stop) {
  Player p;
  CHECK(p.open(makeWav("seek.wav", 440.0, 4.0)));
  p.play();
  p.seek(2.5);
  CHECK_NEAR(p.position(), 2.5, 0.001);
  std::vector<float> out(441 * 2);
  p.render(out.data(), 441);
  CHECK_NEAR(p.position(), 2.51, 0.001);
  p.seek(99.0);  // clamps inside the track
  CHECK(p.position() <= 4.0);
  p.seek(-5.0);
  CHECK_NEAR(p.position(), 0.0, 1e-9);
  p.seek(1.0);
  p.stop();
  CHECK(p.state() == PlayState::Stopped);
  CHECK_NEAR(p.position(), 0.0, 1e-9);
}

TEST(player_volume_scales_the_output) {
  Player p;
  CHECK(p.open(makeWav("vol.wav", 440.0, 1.0)));
  p.play();
  std::vector<float> out(4410 * 2);
  p.setVolume(1.0f);
  p.render(out.data(), 4410);
  const float full = peaks(out).l;
  p.seek(0);
  p.setVolume(0.5f);
  p.render(out.data(), 4410);
  CHECK_NEAR(peaks(out).l, full * 0.5f, 0.02);
  p.seek(0);
  p.setVolume(0.0f);
  p.render(out.data(), 4410);
  CHECK(peaks(out).l == 0.0f);
}

TEST(player_balance_pans_between_channels) {
  Player p;
  CHECK(p.open(makeWav("bal.wav", 440.0, 1.0)));
  p.play();
  p.setVolume(1.0f);
  std::vector<float> out(2205 * 2);
  p.setBalance(-1.0f);
  p.render(out.data(), 2205);
  CHECK(peaks(out).l > 0.3f);
  CHECK(peaks(out).r == 0.0f);
  p.seek(0);
  p.setBalance(1.0f);
  p.render(out.data(), 2205);
  CHECK(peaks(out).l == 0.0f);
  CHECK(peaks(out).r > 0.3f);
  p.seek(0);
  p.setBalance(0.0f);
  p.render(out.data(), 2205);
  CHECK(peaks(out).l > 0.3f && peaks(out).r > 0.3f);
}

TEST(player_resamples_other_formats_to_44100) {
  // 22.05 kHz mono source
  std::vector<float> mono(22050);
  for (std::size_t i = 0; i < mono.size(); ++i)
    mono[i] = static_cast<float>(0.5 * std::sin(2.0 * M_PI * 440.0 * i / 22050.0));
  const auto f = (testing::scratch() / "mono22k.wav").string();
  CHECK(writeWav16(f, mono, 1, 22050));
  Player p;
  CHECK(p.open(f));
  const TrackInfo i = p.info();
  CHECK_EQ(i.sourceRate, 22050u);
  CHECK_EQ(i.sourceChannels, 1u);
  CHECK_NEAR(i.duration, 1.0, 0.01);
  p.play();
  std::vector<float> out(44100 * 2);
  p.render(out.data(), 44100);
  CHECK(peaks(out).l > 0.3f);
  CHECK_NEAR(peaks(out).l, peaks(out).r, 1e-4);  // mono is duplicated to both channels
}

TEST(player_eq_is_in_the_signal_path) {
  Player p;
  CHECK(p.open(makeWav("eq.wav", 1000.0, 2.0, 0.1)));
  p.setVolume(1.0f);
  p.play();
  std::vector<float> out(22050 * 2);
  p.render(out.data(), 22050);
  const float flat = peaks(out).l;
  p.seek(0);
  p.setEqGain(4, 12.0f);
  CHECK_NEAR(p.eqGain(4), 12.0, 1e-6);
  p.render(out.data(), 22050);
  CHECK(peaks(out).l > flat * 3.0f);  // ~ +12 dB
  p.setEqEnabled(false);
  p.seek(0);
  p.render(out.data(), 22050);
  CHECK_NEAR(peaks(out).l, flat, 0.01);
  CHECK_NEAR(p.eqResponse({1000.0f})[0], 12.0, 0.2);
}

TEST(player_feeds_the_visualizer_tap) {
  Player p;
  CHECK(p.open(makeWav("tap.wav", 440.0, 1.0)));
  p.play();
  std::vector<float> out(2048 * 2);
  p.render(out.data(), 2048);
  std::vector<float> tap(1024);
  p.copyLatest(tap.data(), tap.size());
  float peak = 0;
  for (float v : tap) peak = std::max(peak, std::fabs(v));
  CHECK(peak > 0.2f);
}

TEST(player_device_starts_and_stops) {
  Player p;
  std::string err;
  if (!p.startDevice(&err)) {
    std::printf("    (no audio backend here: %s - skipping)\n", err.c_str());
    return;
  }
  CHECK(p.deviceRunning());
  p.stopDevice();
  CHECK(!p.deviceRunning());
}
