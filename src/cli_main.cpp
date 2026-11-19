// wenamp-cli: headless front-end. Everything the window does, minus the window:
// inspect files, render the UI to an image, run the analyzer, generate test audio.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <complex>
#include <string>
#include <thread>
#include <vector>

#include "wenamp/eq.hpp"
#include "wenamp/fft.hpp"
#include "wenamp/player.hpp"
#include "wenamp/playlist.hpp"
#include "wenamp/spectrum.hpp"
#include "wenamp/tags.hpp"
#include "wenamp/ui.hpp"
#include "wenamp/version.hpp"
#include "wenamp/wav.hpp"

namespace fs = std::filesystem;
using namespace wenamp;

namespace {

void usage() {
  std::puts(
      "wenamp-cli " "0.1.0" " - headless front-end for the wenamp engine\n"
      "\n"
      "usage: wenamp-cli <command> [options]\n"
      "\n"
      "  info <file>                         format, duration, bitrate and tags\n"
      "  analyze <file> [--seconds N]        decode and print the 19-bar spectrum over time\n"
      "  render-ui <out.ppm> [options] [files...]\n"
      "        --theme classic|ember|paper   skin colours (default classic)\n"
      "        --eq                          show the equalizer window\n"
      "        --playlist                    show the playlist window\n"
      "        --preset <name>               apply an EQ preset (Rock, Pop, Jazz, ...)\n"
      "        --at <seconds>                play this long (offline) before capturing\n"
      "        --scale <k>                   integer upscale (default 1)\n"
      "        --demo                        play a generated demo loop when no files given\n"
      "  gen-tone <out.wav> [hz] [seconds]   write a stereo sine wave\n"
      "  gen-demo <out.wav> [seconds]        write a short chord-and-beat demo loop\n"
      "  play <files...> [--seconds N]       play through the audio device (null backend on CI)\n"
      "  bench                               DSP throughput numbers\n"
      "  selftest                            quick end-to-end sanity checks\n"
      "  version");
}

bool hasFlag(std::vector<std::string>& a, const char* f) {
  auto it = std::find(a.begin(), a.end(), f);
  if (it == a.end()) return false;
  a.erase(it);
  return true;
}

std::string takeOpt(std::vector<std::string>& a, const char* f, const std::string& def = "") {
  auto it = std::find(a.begin(), a.end(), f);
  if (it == a.end() || it + 1 == a.end()) return def;
  std::string v = *(it + 1);
  a.erase(it, it + 2);
  return v;
}

int cmdInfo(const std::vector<std::string>& a) {
  if (a.empty()) return usage(), 2;
  Player p;
  std::string err;
  if (!p.open(a[0], &err)) {
    std::fprintf(stderr, "error: %s\n", err.c_str());
    return 1;
  }
  const TrackInfo i = p.info();
  std::printf("file      : %s\n", i.path.c_str());
  std::printf("duration  : %s (%.3f s)\n", Playlist::formatTime(i.duration).c_str(), i.duration);
  std::printf("format    : %u Hz, %u ch\n", i.sourceRate, i.sourceChannels);
  std::printf("bitrate   : %u kbps (average)\n", i.bitrateKbps);
  Tags t;
  if (readTags(a[0], t)) {
    std::printf("title     : %s\nartist    : %s\nalbum     : %s\n", t.title.c_str(), t.artist.c_str(), t.album.c_str());
  } else {
    std::printf("tags      : none\n");
  }
  return 0;
}

// Drive the player offline in 1/60 s steps, like the window loop would.
struct Offline {
  Player& player;
  Ui& ui;
  std::vector<float> buf = std::vector<float>(735 * 2);
  void run(double seconds) {
    const int steps = static_cast<int>(std::lround(seconds * 60.0));
    for (int i = 0; i < steps; ++i) {
      player.render(buf.data(), 735);
      ui.tick(1.0 / 60.0);
    }
  }
};

int cmdAnalyze(std::vector<std::string> a) {
  const double secs = std::atof(takeOpt(a, "--seconds", "3").c_str());
  if (a.empty()) return usage(), 2;
  Player p;
  Playlist pl;
  std::string err;
  if (!p.open(a[0], &err)) {
    std::fprintf(stderr, "error: %s\n", err.c_str());
    return 1;
  }
  p.play();
  SpectrumAnalyzer an;
  std::vector<float> buf(735 * 2), mono(1024);
  const int steps = static_cast<int>(secs * 60);
  const int every = std::max(1, steps / 12);
  std::printf("19-bar spectrum, 50 Hz .. 16 kHz (log), one row every %.2f s\n", every / 60.0);
  for (int s = 0; s < steps; ++s) {
    p.render(buf.data(), 735);
    p.copyLatest(mono.data(), 1024);
    an.process(mono.data(), 1024, 1.0f / 60.0f);
    if (s % every == 0) {
      std::printf("%5.2fs  ", s / 60.0);
      for (float v : an.bars()) {
        static const char* ramp[] = {" ", ".", ":", "-", "=", "+", "*", "#", "%", "@"};
        std::printf("%s%s", ramp[std::min(9, static_cast<int>(v * 9.99f))], ramp[std::min(9, static_cast<int>(v * 9.99f))]);
      }
      std::printf("\n");
    }
  }
  return 0;
}

int cmdRenderUi(std::vector<std::string> a) {
  const std::string themeName = takeOpt(a, "--theme", "classic");
  const std::string preset = takeOpt(a, "--preset", "");
  const double at = std::atof(takeOpt(a, "--at", "0").c_str());
  const int scale = std::max(1, std::atoi(takeOpt(a, "--scale", "1").c_str()));
  const bool eq = hasFlag(a, "--eq");
  const bool plw = hasFlag(a, "--playlist");
  const bool demo = hasFlag(a, "--demo");
  if (a.empty()) return usage(), 2;
  const std::string out = a[0];
  std::vector<std::string> files(a.begin() + 1, a.end());

  if (files.empty() && demo) {
    const fs::path f = fs::temp_directory_path() / "wenamp demo - a minor ninth.wav";
    writeWav16(f.string(), makeDemoMusic(30.0), 2, 44100);
    files.push_back(f.string());
  }

  Player player;
  Playlist playlist;
  Ui ui(player, playlist, Theme::byName(themeName));
  ui.showEq(eq);
  ui.showPlaylist(plw);
  for (const auto& f : files) {
    if (f.size() > 4 && (f.rfind(".m3u") == f.size() - 4 || f.rfind(".m3u8") == f.size() - 5)) playlist.loadM3U(f);
    else playlist.addPath(f);
  }
  if (!preset.empty()) {
    bool found = false;
    for (std::size_t i = 0; i < eqPresets().size(); ++i)
      if (preset == eqPresets()[i].name) {
        ui.setEqPreset(static_cast<int>(i));
        found = true;
      }
    if (!found) {
      std::fprintf(stderr, "error: unknown EQ preset '%s'\n", preset.c_str());
      return 2;
    }
  }
  if (!playlist.empty()) ui.playIndex(0);
  Offline off{player, ui};
  off.run(at);

  Canvas c;
  ui.render(c);
  const Canvas scaled = scale > 1 ? c.scaled(scale) : c;
  if (!scaled.writePPM(out)) {
    std::fprintf(stderr, "error: cannot write %s\n", out.c_str());
    return 1;
  }
  std::printf("wrote %s (%dx%d)\n", out.c_str(), scaled.width(), scaled.height());
  return 0;
}

int cmdGenTone(const std::vector<std::string>& a) {
  if (a.empty()) return usage(), 2;
  const double hz = a.size() > 1 ? std::atof(a[1].c_str()) : 440.0;
  const double secs = a.size() > 2 ? std::atof(a[2].c_str()) : 5.0;
  if (!writeWav16(a[0], makeTone(hz, secs), 2, 44100)) {
    std::fprintf(stderr, "error: cannot write %s\n", a[0].c_str());
    return 1;
  }
  std::printf("wrote %s (%.1f Hz, %.1f s)\n", a[0].c_str(), hz, secs);
  return 0;
}

int cmdGenDemo(const std::vector<std::string>& a) {
  if (a.empty()) return usage(), 2;
  const double secs = a.size() > 1 ? std::atof(a[1].c_str()) : 20.0;
  if (!writeWav16(a[0], makeDemoMusic(secs), 2, 44100)) {
    std::fprintf(stderr, "error: cannot write %s\n", a[0].c_str());
    return 1;
  }
  std::printf("wrote %s (%.1f s demo loop)\n", a[0].c_str(), secs);
  return 0;
}

int cmdPlay(std::vector<std::string> a) {
  const double limit = std::atof(takeOpt(a, "--seconds", "0").c_str());
  if (a.empty()) return usage(), 2;
  Player p;
  std::string err;
  if (!p.startDevice(&err)) {
    std::fprintf(stderr, "audio: %s\n", err.c_str());
    return 1;
  }
  for (const auto& f : a) {
    if (!p.open(f, &err)) {
      std::fprintf(stderr, "skip: %s\n", err.c_str());
      continue;
    }
    std::printf("playing %s (%s)\n", f.c_str(), Playlist::formatTime(p.duration()).c_str());
    p.play();
    const auto t0 = std::chrono::steady_clock::now();
    while (p.state() == PlayState::Playing) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      if (limit > 0 && std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() >= limit) break;
    }
    p.stop();
  }
  return 0;
}

int cmdBench() {
  const std::size_t frames = 44100 * 10;
  std::vector<float> buf(frames * 2);
  const auto tone = makeTone(1000.0, 10.0);
  std::copy(tone.begin(), tone.end(), buf.begin());
  Equalizer eq;
  for (int i = 0; i < kEqBands; ++i) eq.setGain(i, (i % 2) ? 4.0f : -3.0f);
  auto t0 = std::chrono::steady_clock::now();
  eq.process(buf.data(), frames);
  double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  std::printf("10-band EQ     : %6.1fx realtime (10 s stereo in %.1f ms)\n", 10.0 / dt, dt * 1000);

  SpectrumAnalyzer an;
  std::vector<float> mono(1024, 0.25f);
  t0 = std::chrono::steady_clock::now();
  const int iters = 6000;
  for (int i = 0; i < iters; ++i) an.process(mono.data(), mono.size(), 1.0f / 60.0f);
  dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  std::printf("1024-pt spectrum: %6.0f frames/s (%.2f us each)\n", iters / dt, dt / iters * 1e6);

  Canvas c;
  Player p;
  Playlist pl;
  Ui ui(p, pl);
  ui.showEq(true);
  ui.showPlaylist(true);
  t0 = std::chrono::steady_clock::now();
  const int frames2 = 600;
  for (int i = 0; i < frames2; ++i) ui.render(c);
  dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  std::printf("UI render       : %6.0f fps (275x%d software frame)\n", frames2 / dt, ui.windowHeight());
  return 0;
}

int fails = 0;
void check(bool ok, const char* what) {
  std::printf("  [%s] %s\n", ok ? "pass" : "FAIL", what);
  if (!ok) ++fails;
}

int cmdSelfTest() {
  std::puts("wenamp selftest");
  // FFT: a bin-centred sine lands in exactly its bin.
  {
    FFT f(1024);
    std::vector<std::complex<float>> d(1024);
    for (int i = 0; i < 1024; ++i) d[i] = {std::sin(2.0f * 3.14159265f * 64.0f * i / 1024.0f), 0.0f};
    f.forward(d);
    int peak = 0;
    for (int i = 1; i < 512; ++i)
      if (std::abs(d[i]) > std::abs(d[peak])) peak = i;
    check(peak == 64, "FFT places a 64-cycle sine in bin 64");
  }
  // EQ: +12 dB at 1 kHz raises a 1 kHz tone by ~12 dB.
  {
    Equalizer eq;
    eq.setGain(4, 12.0f);
    auto tone = makeTone(1000.0, 1.0, 44100, 0.1);
    auto boosted = tone;
    eq.process(boosted.data(), boosted.size() / 2);
    double a = 0, b = 0;
    for (std::size_t i = 22050; i < tone.size(); ++i) {
      a += tone[i] * tone[i];
      b += boosted[i] * boosted[i];
    }
    const double db = 10.0 * std::log10(b / a);
    check(std::fabs(db - 12.0) < 0.6, "EQ +12 dB band boosts a 1 kHz tone by ~12 dB");
  }
  // Player: decode a generated WAV end to end.
  {
    const fs::path f = fs::temp_directory_path() / "wenamp-selftest.wav";
    writeWav16(f.string(), makeTone(440.0, 1.0), 2, 44100);
    Player p;
    std::string err;
    const bool opened = p.open(f.string(), &err);
    check(opened, "Player opens a 16-bit WAV");
    if (opened) {
      check(std::fabs(p.duration() - 1.0) < 0.01, "duration is ~1.0 s");
      p.play();
      std::vector<float> out(4410 * 2);
      p.render(out.data(), 4410);
      float peak = 0;
      for (float v : out) peak = std::max(peak, std::fabs(v));
      check(peak > 0.2f, "rendered audio is non-silent");
      check(std::fabs(p.position() - 0.1) < 0.001, "position advances with rendered frames");
    }
    fs::remove(f);
  }
  // UI: clicking Play on a one-track playlist starts playback.
  {
    const fs::path f = fs::temp_directory_path() / "wenamp-selftest-ui.wav";
    writeWav16(f.string(), makeTone(440.0, 1.0), 2, 44100);
    Player p;
    Playlist pl;
    pl.addPath(f.string());
    Ui ui(p, pl);
    ui.mouseDown(50, 95);
    ui.mouseUp(50, 95);
    check(p.state() == PlayState::Playing, "UI Play button starts the player");
    fs::remove(f);
  }
  std::printf("%s\n", fails ? "selftest FAILED" : "selftest ok");
  return fails ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return usage(), 2;
  const std::string cmd = argv[1];
  std::vector<std::string> rest(argv + 2, argv + argc);
  if (cmd == "info") return cmdInfo(rest);
  if (cmd == "analyze") return cmdAnalyze(rest);
  if (cmd == "render-ui") return cmdRenderUi(rest);
  if (cmd == "gen-tone") return cmdGenTone(rest);
  if (cmd == "gen-demo") return cmdGenDemo(rest);
  if (cmd == "play") return cmdPlay(rest);
  if (cmd == "bench") return cmdBench();
  if (cmd == "selftest") return cmdSelfTest();
  if (cmd == "version" || cmd == "--version") return std::printf("wenamp %s\n", kVersion), 0;
  if (cmd == "help" || cmd == "--help" || cmd == "-h") return usage(), 0;
  std::fprintf(stderr, "unknown command: %s\n", cmd.c_str());
  return usage(), 2;
}
