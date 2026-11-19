#pragma once
#include <string>

#include "wenamp/canvas.hpp"
#include "wenamp/player.hpp"
#include "wenamp/playlist.hpp"
#include "wenamp/spectrum.hpp"
#include "wenamp/theme.hpp"

namespace wenamp {

enum class Region {
  None,
  TitleBar, Close,
  Prev, Play, Pause, Stop, Next, Eject, Shuffle, Repeat,
  EqToggle, PlToggle,
  Seek, Volume, Balance, Time, Vis, Marquee,
  EqOn, EqPresets, EqPreamp, EqBand,   // EqBand: index = band 0..9
  PlRow, PlList, PlScroll, PlRem, PlClr  // PlRow: index = playlist entry
};

struct Hit {
  Region region = Region::None;
  int index = -1;
  bool operator==(const Hit& o) const { return region == o.region && index == o.index; }
};

enum class Key {
  Prev, Play, Pause, Stop, Next,
  SeekBack, SeekFwd, VolUp, VolDown,
  Shuffle, Repeat, ToggleEq, TogglePlaylist, TogglePause,
  DeleteSelected, NextTheme
};

// Window controller + software renderer. The three classic windows (main,
// equalizer, playlist) are stacked in one 275 px wide surface, "docked" the
// way the original docks them. All input arrives as plain coordinates, so the
// SDL front-end, the CLI and the tests drive exactly the same code path.
class Ui {
 public:
  static constexpr int kWidth = 275;
  static constexpr int kMainH = 116;
  static constexpr int kEqH = 116;
  static constexpr int kPlH = 154;
  static constexpr int kTitleH = 14;

  Ui(Player& player, Playlist& playlist, Theme theme = Theme::classic());

  // Layout
  void showEq(bool on) { showEq_ = on; }
  bool eqVisible() const { return showEq_; }
  void showPlaylist(bool on) { showPl_ = on; }
  bool playlistVisible() const { return showPl_; }
  int windowHeight() const { return kMainH + (showEq_ ? kEqH : 0) + (showPl_ ? kPlH : 0); }

  // Theme
  void setTheme(const Theme& t) { theme_ = t; }
  const Theme& theme() const { return theme_; }

  // Input (coordinates are in unscaled UI pixels)
  Hit hitTest(int x, int y) const;
  bool isDragRegion(int x, int y) const { return hitTest(x, y).region == Region::TitleBar; }
  void mouseDown(int x, int y, bool doubleClick = false);
  void mouseUp(int x, int y);
  void mouseMove(int x, int y);
  void wheel(int x, int y, int dy);  // dy > 0 scrolls up
  void key(Key k);

  // Frame update + draw
  void tick(double dt);
  void render(Canvas& out) const;  // resizes `out` to kWidth x windowHeight()

  // Actions (also used by keys / buttons)
  void playIndex(int i);
  void actPlay();
  void actNext();
  void actPrev();
  void setStatus(const std::string& s, double seconds = 2.0);
  void setEqPreset(int index);  // applies eqPresets()[index] to the player

  // State (tests, window title)
  bool wantsQuit() const { return quit_; }
  int selectedRow() const { return selected_; }
  bool showRemaining() const { return remaining_; }
  int eqPresetIndex() const { return eqPreset_; }
  const std::string& status() const { return status_; }
  std::string titleLine() const;  // "3. Artist - Title (3:41)" or the idle banner
  const SpectrumAnalyzer& analyzer() const { return analyzer_; }

 private:
  enum class Drag { None, Seek, Volume, Balance, EqBand, EqPreamp, PlScroll };

  int eqTop() const { return kMainH; }
  int plTop() const { return kMainH + (showEq_ ? kEqH : 0); }
  Hit hitMain(int x, int y) const;
  Hit hitEq(int x, int y) const;
  Hit hitPl(int x, int y) const;
  void dragTo(int x, int y);
  void press(const Hit& h, bool doubleClick);
  int visibleRows() const;
  void clampScroll();

  void drawMain(Canvas& c) const;
  void drawEq(Canvas& c) const;
  void drawPlaylist(Canvas& c) const;

  Player& player_;
  Playlist& playlist_;
  Theme theme_;
  SpectrumAnalyzer analyzer_;

  bool showEq_ = false;
  bool showPl_ = false;
  bool remaining_ = false;
  bool quit_ = false;
  Drag drag_ = Drag::None;
  int dragIndex_ = -1;
  double dragSeek_ = 0.0;
  Hit pressed_;
  int selected_ = -1;
  int scroll_ = 0;
  int eqPreset_ = 0;
  double marquee_ = 0.0;
  double clock_ = 0.0;
  std::string status_;
  double statusT_ = 0.0;
};

}  // namespace wenamp
