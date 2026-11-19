#include <string>
#include <vector>

#include "testing.hpp"
#include "wenamp/canvas.hpp"
#include "wenamp/font.hpp"
#include "wenamp/player.hpp"
#include "wenamp/playlist.hpp"
#include "wenamp/ui.hpp"
#include "wenamp/wav.hpp"

using namespace wenamp;

namespace {

struct Rig {
  Player player;
  Playlist playlist;
  Ui ui;
  explicit Rig(int tracks = 3, double seconds = 4.0) : ui(player, playlist) {
    for (int i = 0; i < tracks; ++i) {
      const auto f = (testing::scratch() / ("ui-track-" + std::to_string(i) + ".wav")).string();
      writeWav16(f, makeTone(220.0 * (i + 1), seconds, 44100, 0.4), 2, 44100);
      playlist.addPath(f);
    }
  }
  void click(int x, int y, bool dbl = false) {
    ui.mouseDown(x, y, dbl);
    ui.mouseUp(x, y);
  }
  void pull(double seconds) {
    std::vector<float> out(735 * 2);
    const int steps = static_cast<int>(seconds * 60);
    for (int i = 0; i < steps; ++i) {
      player.render(out.data(), 735);
      ui.tick(1.0 / 60.0);
    }
  }
};

}  // namespace

TEST(ui_hit_testing_main_window) {
  Rig r(1);
  struct Case {
    int x, y;
    Region want;
  };
  const Case cases[] = {
      {100, 5, Region::TitleBar}, {267, 7, Region::Close},   {20, 95, Region::Prev},
      {50, 95, Region::Play},     {70, 95, Region::Pause},   {95, 95, Region::Stop},
      {120, 95, Region::Next},    {145, 95, Region::Eject},  {180, 95, Region::Shuffle},
      {230, 95, Region::Repeat},  {230, 63, Region::EqToggle}, {250, 63, Region::PlToggle},
      {100, 76, Region::Seek},    {130, 62, Region::Volume}, {190, 62, Region::Balance},
      {60, 30, Region::Time},     {50, 50, Region::Vis},     {150, 28, Region::Marquee},
      {5, 110, Region::None},
  };
  for (const auto& c : cases) CHECK(r.ui.hitTest(c.x, c.y).region == c.want);
  CHECK(r.ui.hitTest(-1, 5).region == Region::None);
  CHECK(r.ui.hitTest(275, 5).region == Region::None);
  CHECK(r.ui.hitTest(100, 500).region == Region::None);  // below the stack
}

TEST(ui_title_bar_is_draggable_except_the_close_button) {
  Rig r(1);
  CHECK(r.ui.isDragRegion(100, 5));
  CHECK(!r.ui.isDragRegion(267, 7));
  CHECK(!r.ui.isDragRegion(100, 50));
}

TEST(ui_window_stack_grows_with_the_docked_windows) {
  Rig r(1);
  CHECK_EQ(r.ui.windowHeight(), Ui::kMainH);
  r.ui.showEq(true);
  CHECK_EQ(r.ui.windowHeight(), Ui::kMainH + Ui::kEqH);
  r.ui.showPlaylist(true);
  CHECK_EQ(r.ui.windowHeight(), Ui::kMainH + Ui::kEqH + Ui::kPlH);

  const Hit band = r.ui.hitTest(78 + 18 * 3 + 7, Ui::kMainH + 60);
  CHECK(band.region == Region::EqBand);
  CHECK_EQ(band.index, 3);
  CHECK(r.ui.hitTest(20, Ui::kMainH + 60).region == Region::EqPreamp);
  CHECK(r.ui.hitTest(100, Ui::kMainH + Ui::kEqH + 20).region == Region::PlRow);

  r.ui.showEq(false);  // playlist slides up under the main window
  CHECK(r.ui.hitTest(100, Ui::kMainH + 20).region == Region::PlRow);
}

TEST(ui_transport_buttons_drive_the_player) {
  Rig r(3);
  r.click(50, 95);  // Play
  CHECK(r.player.state() == PlayState::Playing);
  CHECK_EQ(r.playlist.current(), 0);
  r.click(70, 95);  // Pause
  CHECK(r.player.state() == PlayState::Paused);
  r.click(70, 95);
  CHECK(r.player.state() == PlayState::Playing);
  r.click(120, 95);  // Next
  CHECK_EQ(r.playlist.current(), 1);
  CHECK(r.player.state() == PlayState::Playing);
  r.click(20, 95);  // Prev
  CHECK_EQ(r.playlist.current(), 0);
  r.click(95, 95);  // Stop
  CHECK(r.player.state() == PlayState::Stopped);
  CHECK_NEAR(r.player.position(), 0.0, 1e-9);
}

TEST(ui_play_with_an_empty_playlist_explains_itself) {
  Player p;
  Playlist pl;
  Ui ui(p, pl);
  ui.mouseDown(50, 95);
  ui.mouseUp(50, 95);
  CHECK(p.state() == PlayState::Stopped);
  CHECK(!ui.status().empty());
}

TEST(ui_volume_and_balance_sliders_drag) {
  Rig r(1);
  r.ui.mouseDown(107 + 5, 62);  // far left of the track
  CHECK_NEAR(r.player.volume(), 0.0, 0.02);
  r.ui.mouseMove(107 + 68 - 5, 62);
  CHECK_NEAR(r.player.volume(), 1.0, 0.02);
  r.ui.mouseMove(107 + 34, 62);
  CHECK_NEAR(r.player.volume(), 0.5, 0.03);
  r.ui.mouseUp(107 + 34, 62);
  r.ui.mouseMove(10, 10);  // no drag in progress: ignored
  CHECK_NEAR(r.player.volume(), 0.5, 0.03);

  r.ui.mouseDown(177 + 19, 62);  // exact centre of the 38 px track -> detent
  CHECK_NEAR(r.player.balance(), 0.0, 1e-6);
  r.ui.mouseMove(177 + 5, 62);
  CHECK_NEAR(r.player.balance(), -1.0, 0.02);
  r.ui.mouseUp(0, 0);
}

TEST(ui_seek_bar_applies_on_release) {
  Rig r(1, 4.0);
  r.click(50, 95);  // play
  r.ui.mouseDown(140, 76);
  CHECK(r.player.position() < 0.5);  // not applied yet
  r.ui.mouseMove(140, 76);
  r.ui.mouseUp(140, 76);
  CHECK_NEAR(r.player.position(), 2.0, 0.15);
}

TEST(ui_seek_bar_ignored_without_a_track) {
  Player p;
  Playlist pl;
  Ui ui(p, pl);
  ui.mouseDown(140, 76);
  ui.mouseUp(140, 76);
  CHECK_NEAR(p.position(), 0.0, 1e-9);
}

TEST(ui_shuffle_repeat_and_time_toggles) {
  Rig r(2);
  r.click(180, 95);
  CHECK(r.playlist.shuffle());
  r.click(180, 95);
  CHECK(!r.playlist.shuffle());
  r.click(230, 95);
  CHECK(r.playlist.repeat() == RepeatMode::All);
  r.click(230, 95);
  CHECK(r.playlist.repeat() == RepeatMode::One);
  CHECK(!r.ui.showRemaining());
  r.click(60, 30);
  CHECK(r.ui.showRemaining());
  r.click(230, 63);
  CHECK(r.ui.eqVisible());
  r.click(250, 63);
  CHECK(r.ui.playlistVisible());
}

TEST(ui_close_button_requests_quit) {
  Rig r(1);
  CHECK(!r.ui.wantsQuit());
  r.click(267, 7);
  CHECK(r.ui.wantsQuit());
}

TEST(ui_equalizer_sliders_and_presets) {
  Rig r(1);
  r.ui.showEq(true);
  const int top = Ui::kMainH;
  const int bandX = 78 + 18 * 4 + 7;
  r.ui.mouseDown(bandX, top + 52 + 3);  // very top of band 4 -> +12 dB
  CHECK(r.player.eqGain(4) > 11.0f);
  r.ui.mouseMove(bandX, top + 52 + 22);  // middle of the 44 px track -> centre detent
  CHECK_NEAR(r.player.eqGain(4), 0.0, 1e-6);
  r.ui.mouseMove(bandX, top + 52 + 44);  // bottom
  CHECK(r.player.eqGain(4) < -11.0f);
  r.ui.mouseUp(bandX, top + 60);

  r.ui.mouseDown(21 + 7, top + 52 + 3);  // preamp
  CHECK(r.player.eqPreamp() > 11.0f);
  r.ui.mouseUp(0, 0);

  r.click(210, top + 22);  // presets button
  CHECK_EQ(r.ui.eqPresetIndex(), 1);
  CHECK_NEAR(r.player.eqGain(0), eqPresets()[1].gains[0], 1e-6);

  r.click(25, top + 22);  // ON toggle
  CHECK(!r.player.eqEnabled());
}

TEST(ui_playlist_selection_double_click_scroll_and_edit) {
  Rig r(20, 1.0);
  r.ui.showPlaylist(true);
  const int top = Ui::kMainH;
  r.click(100, top + 17 + 9 * 1 + 2);  // second row
  CHECK_EQ(r.ui.selectedRow(), 1);
  CHECK(r.player.state() == PlayState::Stopped);
  r.click(100, top + 17 + 9 * 2 + 2, true);  // double-click third row
  CHECK_EQ(r.playlist.current(), 2);
  CHECK(r.player.state() == PlayState::Playing);

  r.ui.wheel(100, top + 40, -1);  // scroll down
  CHECK(r.ui.hitTest(100, top + 17 + 2).index > 0);
  r.ui.wheel(100, top + 40, +5);  // scroll back to the top
  CHECK_EQ(r.ui.hitTest(100, top + 17 + 2).index, 0);

  const std::size_t before = r.playlist.size();
  r.click(100, top + 17 + 9 * 5 + 2);
  r.click(20, top + 132 + 3);  // REM
  CHECK_EQ(r.playlist.size(), before - 1);

  r.click(52, top + 132 + 3);  // CLR
  CHECK(r.playlist.empty());
  CHECK(!r.player.hasTrack());
}

TEST(ui_keys) {
  Rig r(3);
  r.ui.key(Key::Play);
  CHECK(r.player.state() == PlayState::Playing);
  r.ui.key(Key::TogglePause);
  CHECK(r.player.state() == PlayState::Paused);
  r.ui.key(Key::Stop);
  CHECK(r.player.state() == PlayState::Stopped);

  r.player.setVolume(0.5f);
  r.ui.key(Key::VolUp);
  CHECK_NEAR(r.player.volume(), 0.55, 1e-4);
  r.ui.key(Key::VolDown);
  r.ui.key(Key::VolDown);
  CHECK_NEAR(r.player.volume(), 0.45, 1e-4);

  r.ui.key(Key::Play);
  r.ui.key(Key::SeekFwd);
  CHECK_NEAR(r.player.position(), 5.0 > r.player.duration() ? r.player.duration() : 5.0, 0.05);
  r.ui.key(Key::SeekBack);
  CHECK(r.player.position() < 0.5);

  r.ui.key(Key::ToggleEq);
  CHECK(r.ui.eqVisible());
  r.ui.key(Key::TogglePlaylist);
  CHECK(r.ui.playlistVisible());

  const std::string before = r.ui.theme().name;
  r.ui.key(Key::NextTheme);
  CHECK(r.ui.theme().name != before);

  r.ui.key(Key::Next);
  CHECK_EQ(r.playlist.current(), 1);
  r.ui.key(Key::Prev);
  CHECK_EQ(r.playlist.current(), 0);
}

TEST(ui_auto_advances_and_stops_at_the_end) {
  Rig r(2, 0.3);
  r.ui.playIndex(0);
  r.pull(0.5);  // first track ends, tick() advances
  CHECK_EQ(r.playlist.current(), 1);
  CHECK(r.player.state() == PlayState::Playing);
  r.pull(0.5);  // last track ends
  CHECK(r.player.state() == PlayState::Stopped);

  Rig loop(2, 0.3);
  loop.playlist.setRepeat(RepeatMode::All);
  loop.ui.playIndex(1);
  loop.pull(0.5);
  CHECK_EQ(loop.playlist.current(), 0);
  CHECK(loop.player.state() == PlayState::Playing);
}

TEST(ui_visualizer_reacts_to_audio_and_settles_when_stopped) {
  Rig r(1, 4.0);
  r.click(50, 95);
  r.pull(0.5);
  float peak = 0;
  for (float b : r.ui.analyzer().bars()) peak = std::max(peak, b);
  CHECK(peak > 0.5f);
  r.click(95, 95);  // stop
  r.pull(1.5);
  peak = 0;
  for (float b : r.ui.analyzer().bars()) peak = std::max(peak, b);
  CHECK(peak < 0.05f);
}

TEST(ui_rendering_is_deterministic_and_reflects_state) {
  Rig r(1, 4.0);
  Canvas a, b;
  r.ui.render(a);
  r.ui.render(b);
  CHECK_EQ(a.width(), Ui::kWidth);
  CHECK_EQ(a.height(), Ui::kMainH);
  CHECK_EQ(a.hash(), b.hash());

  r.click(50, 95);
  r.pull(0.5);
  r.ui.render(b);
  CHECK(a.hash() != b.hash());  // playing state, time, bars all differ

  Canvas idle;
  Rig r2(1);
  r2.ui.render(idle);
  r2.ui.key(Key::NextTheme);
  Canvas themed;
  r2.ui.render(themed);
  CHECK(idle.hash() != themed.hash());

  r2.ui.showEq(true);
  r2.ui.showPlaylist(true);
  Canvas full;
  r2.ui.render(full);
  CHECK_EQ(full.height(), Ui::kMainH + Ui::kEqH + Ui::kPlH);
}

TEST(ui_every_theme_renders_without_touching_outside_the_frame) {
  for (const auto& name : Theme::names()) {
    Rig r(2);
    r.ui.setTheme(Theme::byName(name));
    r.ui.showEq(true);
    r.ui.showPlaylist(true);
    r.click(50, 95);
    r.pull(0.3);
    Canvas c;
    r.ui.render(c);
    CHECK_EQ(c.width(), Ui::kWidth);
    // the window face is opaque everywhere
    bool opaque = true;
    for (int y = 0; y < c.height(); ++y)
      for (int x = 0; x < c.width(); ++x)
        if ((c.pixel(x, y) >> 24) != 0xFF) opaque = false;
    CHECK(opaque);
  }
}

TEST(ui_title_line_follows_the_track) {
  Rig r(2);
  CHECK(r.ui.titleLine().find("DROP A FILE") != std::string::npos);
  r.ui.playIndex(1);
  CHECK(r.ui.titleLine().rfind("2. ", 0) == 0);
}

TEST(ui_bad_file_reports_an_error_instead_of_crashing) {
  Player p;
  Playlist pl;
  Entry e;
  e.path = (testing::scratch() / "gone.mp3").string();
  e.title = "gone";
  pl.add(e);
  Ui ui(p, pl);
  ui.playIndex(0);
  CHECK(p.state() == PlayState::Stopped);
  CHECK(ui.status().find("ERROR") == 0);
}

TEST(canvas_text_and_primitives) {
  Canvas c(20, 10);
  c.clear(rgb(0, 0, 0));
  c.text(0, 0, "A", rgb(255, 255, 255));
  CHECK(c.pixel(0, 1) == rgb(255, 255, 255));  // 'A' left stem, column 0x7E
  CHECK(c.pixel(0, 0) == rgb(0, 0, 0));        // top of the stem is open
  CHECK_EQ(Canvas::textWidth("abc"), 17);
  CHECK_EQ(Canvas::textWidth(""), 0);
  CHECK(glyph5x7(0x01)[0] == glyph5x7('?')[0]);  // unprintable -> '?'

  c.fillRect(-5, -5, 10, 10, rgb(9, 9, 9));  // clipped
  CHECK(c.pixel(0, 0) == rgb(9, 9, 9));
  CHECK(c.pixel(5, 5) != rgb(9, 9, 9));
  c.set(100, 100, rgb(1, 2, 3));  // out of range is a no-op
  CHECK_EQ(c.pixel(100, 100), 0u);

  const Canvas big = c.scaled(3);
  CHECK_EQ(big.width(), 60);
  CHECK(big.pixel(0, 0) == c.pixel(0, 0));
  CHECK(big.pixel(2, 2) == c.pixel(0, 0));
  CHECK(mix(rgb(0, 0, 0), rgb(200, 100, 50), 0.5f) == rgb(100, 50, 25));
}
