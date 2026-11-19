#include "wenamp/ui.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "wenamp/eq.hpp"
#include "wenamp/version.hpp"

namespace wenamp {

namespace {

// ── Classic 275-wide geometry (see docs/classic-layout.md) ──
struct R { int x, y, w, h; };
constexpr R kClose{263, 3, 9, 9};
constexpr R kPrev{16, 88, 23, 18}, kPlay{39, 88, 23, 18}, kPause{62, 88, 23, 18};
constexpr R kStop{85, 88, 23, 18}, kNext{108, 88, 23, 18}, kEject{136, 89, 22, 16};
constexpr R kShuffle{164, 89, 46, 15}, kRepeat{212, 89, 52, 15};
constexpr R kEqBtn{219, 58, 23, 12}, kPlBtn{242, 58, 23, 12};
constexpr R kSeek{16, 72, 248, 10};
constexpr R kVol{107, 57, 68, 13}, kBal{177, 57, 38, 13};
constexpr R kTime{36, 24, 70, 18};
constexpr R kVis{24, 43, 76, 16};
constexpr R kMarq{111, 24, 154, 9};
constexpr R kInfo{109, 40, 158, 14};
constexpr R kStatus{6, 107, 263, 8};

constexpr int kSeekThumbW = 29;
constexpr int kSliderThumbW = 10;

// Equalizer window
constexpr R kEqOn{14, 18, 26, 12}, kEqPre{205, 18, 56, 12}, kEqGraph{84, 18, 113, 30};
constexpr int kEqSliderY = 52, kEqSliderH = 44, kEqThumbH = 6, kEqSliderW = 14;
constexpr int kEqBandX0 = 78, kEqBandStep = 18, kEqPreX = 21;

// Playlist window
constexpr R kPlList{5, 17, 258, 108};
constexpr R kPlBar{266, 17, 5, 108};
constexpr R kPlRem{6, 132, 30, 13}, kPlClr{38, 132, 30, 13};
constexpr int kRowH = 9;

bool in(int x, int y, const R& r) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }
int clampi(int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); }

std::string mmss(double sec) {
  if (sec < 0) sec = 0;
  const int total = static_cast<int>(sec);
  const int m = std::min(total / 60, 99), s = total % 60;
  char b[16];
  std::snprintf(b, sizeof b, "%02d:%02d", m, s);
  return b;
}

// ── Drawing helpers ──
void well(Canvas& c, const Theme& t, const R& r) {
  c.fillRect(r.x, r.y, r.w, r.h, t.well);
  c.bevel(r.x - 1, r.y - 1, r.w + 2, r.h + 2, t.bodyLo, t.bodyHi);
}

void button(Canvas& c, const Theme& t, const R& r, bool pressed, bool active = false) {
  c.fillRect(r.x, r.y, r.w, r.h, active ? mix(t.buttonFace, t.accent, 0.45f) : t.buttonFace);
  if (pressed) c.bevel(r.x, r.y, r.w, r.h, t.bodyLo, t.bodyHi);
  else c.bevel(r.x, r.y, r.w, r.h, t.bodyHi, t.bodyLo);
}

void triRight(Canvas& c, int x, int y, int h, std::uint32_t col) {
  for (int i = 0; i <= h / 2; ++i) c.vline(x + i, y + i, h - 2 * i, col);
}
void triLeft(Canvas& c, int x, int y, int h, std::uint32_t col) {
  for (int i = 0; i <= h / 2; ++i) c.vline(x + h / 2 - i, y + i, h - 2 * i, col);
}

void textCentered(Canvas& c, const R& r, const std::string& s, std::uint32_t col, int dy = 0) {
  const int w = Canvas::textWidth(s);
  c.text(r.x + (r.w - w) / 2, r.y + (r.h - 7) / 2 + dy, s, col);
}

void titleBar(Canvas& c, const Theme& t, const std::string& title, bool close) {
  for (int y = 0; y < Ui::kTitleH; ++y)
    c.hline(0, y, Ui::kWidth, mix(t.titleA, t.titleB, static_cast<float>(y) / (Ui::kTitleH - 1)));
  c.bevel(0, 0, Ui::kWidth, Ui::kTitleH, t.bodyHi, t.bodyLo);
  // grip lines either side of the title, like the classic drag handle
  const int tw = Canvas::textWidth(title);
  const int tx = (Ui::kWidth - tw) / 2;
  for (int i = 0; i < 3; ++i) {
    c.hline(6, 4 + i * 3, tx - 12, mix(t.titleA, t.titleB, 0.5f));
    c.hline(tx + tw + 6, 4 + i * 3, Ui::kWidth - (tx + tw + 6) - (close ? 16 : 6), mix(t.titleA, t.titleB, 0.5f));
  }
  c.text(tx, 3, title, t.titleText);
  if (close) {
    c.fillRect(kClose.x, kClose.y, kClose.w, kClose.h, t.buttonFace);
    c.bevel(kClose.x, kClose.y, kClose.w, kClose.h, t.bodyHi, t.bodyLo);
    for (int i = 0; i < 5; ++i) {
      c.set(kClose.x + 2 + i, kClose.y + 2 + i, t.buttonIcon);
      c.set(kClose.x + 6 - i, kClose.y + 2 + i, t.buttonIcon);
    }
  }
}

void windowFace(Canvas& c, const Theme& t) {
  c.clear(t.body);
  c.bevel(0, 0, c.width(), c.height(), t.bodyHi, t.bodyLo);
}

// Seven-segment digit, 9 x 13. Segment bits: a b c d e f g.
void segDigit(Canvas& c, const Theme& t, int x, int y, int d, bool lit) {
  static const unsigned char mask[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
  //                                     abcdef bc    abdeg abcdg bcfg  acdfg acdefg abc  all   abcdfg
  // bit0=a bit1=b bit2=c bit3=d bit4=e bit5=f bit6=g
  const int on = (d >= 0 && d <= 9 && lit) ? mask[d] : 0;
  struct Seg { int x, y, w, h; };
  static const Seg segs[7] = {{1, 0, 7, 2}, {7, 1, 2, 5}, {7, 7, 2, 5}, {1, 11, 7, 2},
                              {0, 7, 2, 5}, {0, 1, 2, 5}, {1, 5, 7, 3}};
  for (int s = 0; s < 7; ++s) {
    const bool isOn = (on >> s) & 1;
    c.fillRect(x + segs[s].x, y + segs[s].y, segs[s].w, segs[s].h, isOn ? t.lcdOn : t.lcdDim);
  }
}

std::uint32_t barColor(const Theme& t, int row, int rows) {
  const float f = static_cast<float>(row) / static_cast<float>(rows - 1);
  return f < 0.5f ? mix(t.barLow, t.barMid, f * 2.0f) : mix(t.barMid, t.barHigh, (f - 0.5f) * 2.0f);
}

void hslider(Canvas& c, const Theme& t, const R& r, float frac, int thumbW, bool gradientFill) {
  const int ty = r.y + r.h / 2 - 2;
  c.fillRect(r.x, ty, r.w, 4, t.sliderTrack);
  c.bevel(r.x - 1, ty - 1, r.w + 2, 6, t.bodyLo, t.bodyHi);
  const int tx = r.x + static_cast<int>(std::lround(frac * (r.w - thumbW)));
  if (gradientFill) {
    for (int x = r.x; x < tx + thumbW / 2; ++x)
      c.vline(x, ty, 4, mix(t.barLow, t.barHigh, static_cast<float>(x - r.x) / static_cast<float>(r.w)));
  }
  c.fillRect(tx, r.y, thumbW, r.h, t.sliderThumb);
  c.bevel(tx, r.y, thumbW, r.h, t.bodyHi, t.bodyLo);
  c.vline(tx + thumbW / 2, r.y + 2, r.h - 4, t.bodyLo);
}

void vslider(Canvas& c, const Theme& t, int x, int y, int h, int w, float topFrac) {
  const int cx = x + w / 2;
  c.fillRect(cx - 2, y, 4, h, t.sliderTrack);
  c.bevel(cx - 3, y - 1, 6, h + 2, t.bodyLo, t.bodyHi);
  const int ty = y + static_cast<int>(std::lround(topFrac * (h - kEqThumbH)));
  c.fillRect(x, ty, w, kEqThumbH, t.sliderThumb);
  c.bevel(x, ty, w, kEqThumbH, t.bodyHi, t.bodyLo);
  c.hline(x + 2, ty + kEqThumbH / 2, w - 4, t.bodyLo);
}

float sliderFrac(int pos, int start, int len, int thumb) {
  const float f = static_cast<float>(pos - start - thumb / 2) / static_cast<float>(len - thumb);
  return std::clamp(f, 0.0f, 1.0f);
}

}  // namespace

// ════════════════════════════════════════════════════════════════════════════
Ui::Ui(Player& player, Playlist& playlist, Theme theme)
    : player_(player), playlist_(playlist), theme_(std::move(theme)), analyzer_(kSampleRate, 1024) {}

std::string Ui::titleLine() const {
  const int cur = playlist_.current();
  if (cur >= 0 && player_.hasTrack()) return playlist_.displayTitle(static_cast<std::size_t>(cur));
  if (player_.hasTrack()) return Playlist::stemOf(player_.info().path);
  return std::string("WENAMP ") + kVersion + " - DROP A FILE TO PLAY";
}

void Ui::setStatus(const std::string& s, double seconds) {
  status_ = s;
  statusT_ = seconds;
}

void Ui::setEqPreset(int index) {
  const auto& ps = eqPresets();
  if (index < 0 || index >= static_cast<int>(ps.size())) return;
  eqPreset_ = index;
  player_.applyEqPreset(ps[static_cast<std::size_t>(index)]);
  setStatus(std::string("EQ PRESET: ") + ps[static_cast<std::size_t>(index)].name, 1.5);
}

int Ui::visibleRows() const { return kPlList.h / kRowH; }

void Ui::clampScroll() {
  const int maxScroll = std::max(0, static_cast<int>(playlist_.size()) - visibleRows());
  scroll_ = clampi(scroll_, 0, maxScroll);
}

// ── Hit testing ─────────────────────────────────────────────────────────────
Hit Ui::hitMain(int x, int y) const {
  if (y < kTitleH) return {in(x, y, kClose) ? Region::Close : Region::TitleBar, -1};
  struct B { const R& r; Region g; };
  const B btns[] = {{kPrev, Region::Prev},     {kPlay, Region::Play},     {kPause, Region::Pause},
                    {kStop, Region::Stop},     {kNext, Region::Next},     {kEject, Region::Eject},
                    {kShuffle, Region::Shuffle}, {kRepeat, Region::Repeat}, {kEqBtn, Region::EqToggle},
                    {kPlBtn, Region::PlToggle}, {kSeek, Region::Seek},     {kVol, Region::Volume},
                    {kBal, Region::Balance},   {kTime, Region::Time},     {kVis, Region::Vis},
                    {kMarq, Region::Marquee}};
  for (const B& b : btns)
    if (in(x, y, b.r)) return {b.g, -1};
  return {};
}

Hit Ui::hitEq(int x, int y) const {
  if (y < kTitleH) return {in(x, y, kClose) ? Region::Close : Region::TitleBar, -1};
  if (in(x, y, kEqOn)) return {Region::EqOn, -1};
  if (in(x, y, kEqPre)) return {Region::EqPresets, -1};
  if (y >= kEqSliderY - 2 && y < kEqSliderY + kEqSliderH + 2) {
    if (x >= kEqPreX - 2 && x < kEqPreX + kEqSliderW + 2) return {Region::EqPreamp, -1};
    for (int i = 0; i < kEqBands; ++i) {
      const int sx = kEqBandX0 + i * kEqBandStep;
      if (x >= sx - 2 && x < sx + kEqSliderW + 2) return {Region::EqBand, i};
    }
  }
  return {};
}

Hit Ui::hitPl(int x, int y) const {
  if (y < kTitleH) return {in(x, y, kClose) ? Region::Close : Region::TitleBar, -1};
  if (in(x, y, kPlRem)) return {Region::PlRem, -1};
  if (in(x, y, kPlClr)) return {Region::PlClr, -1};
  if (in(x, y, kPlBar)) return {Region::PlScroll, -1};
  if (in(x, y, kPlList)) {
    const int idx = scroll_ + (y - kPlList.y) / kRowH;
    if (idx >= 0 && idx < static_cast<int>(playlist_.size())) return {Region::PlRow, idx};
    return {Region::PlList, -1};
  }
  return {};
}

Hit Ui::hitTest(int x, int y) const {
  if (x < 0 || y < 0 || x >= kWidth) return {};
  if (y < kMainH) return hitMain(x, y);
  if (showEq_ && y >= eqTop() && y < eqTop() + kEqH) return hitEq(x, y - eqTop());
  if (showPl_ && y >= plTop() && y < plTop() + kPlH) return hitPl(x, y - plTop());
  return {};
}

// ── Actions ─────────────────────────────────────────────────────────────────
void Ui::playIndex(int i) {
  if (i < 0 || i >= static_cast<int>(playlist_.size())) return;
  playlist_.setCurrent(static_cast<std::size_t>(i));
  std::string err;
  if (!player_.open(playlist_.at(static_cast<std::size_t>(i)).path, &err)) {
    setStatus("ERROR: " + err, 3.0);
    player_.stop();
    return;
  }
  playlist_.at(static_cast<std::size_t>(i)).duration = player_.duration();
  player_.play();
  marquee_ = 0.0;
  selected_ = i;
  // keep the playing row in view
  if (i < scroll_) scroll_ = i;
  if (i >= scroll_ + visibleRows()) scroll_ = i - visibleRows() + 1;
  clampScroll();
}

void Ui::actPlay() {
  if (player_.state() == PlayState::Paused) {
    player_.play();
  } else if (player_.hasTrack() && player_.state() == PlayState::Playing) {
    player_.seek(0.0);  // classic behaviour: pressing play while playing restarts
  } else if (player_.hasTrack()) {
    player_.play();
  } else if (!playlist_.empty()) {
    playIndex(playlist_.current() >= 0 ? playlist_.current() : 0);
  } else {
    setStatus("PLAYLIST IS EMPTY - DROP A FILE ON THE WINDOW");
  }
}

void Ui::actNext() {
  const int i = playlist_.next(true);
  if (i >= 0) playIndex(i);
}

void Ui::actPrev() {
  const int i = playlist_.previous();
  if (i >= 0) playIndex(i);
}

void Ui::key(Key k) {
  switch (k) {
    case Key::Prev: actPrev(); break;
    case Key::Play: actPlay(); break;
    case Key::Pause:
    case Key::TogglePause: player_.togglePause(); break;
    case Key::Stop: player_.stop(); break;
    case Key::Next: actNext(); break;
    case Key::SeekBack: player_.seek(player_.position() - 5.0); break;
    case Key::SeekFwd: player_.seek(player_.position() + 5.0); break;
    case Key::VolUp:
      player_.setVolume(player_.volume() + 0.05f);
      setStatus("VOLUME: " + std::to_string(static_cast<int>(std::lround(player_.volume() * 100))) + "%", 1.2);
      break;
    case Key::VolDown:
      player_.setVolume(player_.volume() - 0.05f);
      setStatus("VOLUME: " + std::to_string(static_cast<int>(std::lround(player_.volume() * 100))) + "%", 1.2);
      break;
    case Key::Shuffle:
      playlist_.setShuffle(!playlist_.shuffle());
      setStatus(playlist_.shuffle() ? "SHUFFLE: ON" : "SHUFFLE: OFF", 1.2);
      break;
    case Key::Repeat: {
      playlist_.cycleRepeat();
      const RepeatMode m = playlist_.repeat();
      setStatus(m == RepeatMode::Off ? "REPEAT: OFF" : m == RepeatMode::All ? "REPEAT: ALL" : "REPEAT: ONE", 1.2);
      break;
    }
    case Key::ToggleEq: showEq_ = !showEq_; break;
    case Key::TogglePlaylist: showPl_ = !showPl_; break;
    case Key::DeleteSelected:
      if (selected_ >= 0 && selected_ < static_cast<int>(playlist_.size())) {
        const bool wasCurrent = selected_ == playlist_.current();
        playlist_.remove(static_cast<std::size_t>(selected_));
        if (wasCurrent) player_.stop();
        selected_ = std::min(selected_, static_cast<int>(playlist_.size()) - 1);
        clampScroll();
      }
      break;
    case Key::NextTheme: {
      const auto names = Theme::names();
      std::size_t i = 0;
      for (; i < names.size(); ++i)
        if (names[i] == theme_.name) break;
      theme_ = Theme::byName(names[(i + 1) % names.size()]);
      setStatus("THEME: " + theme_.name, 1.2);
      break;
    }
  }
}

// ── Mouse ───────────────────────────────────────────────────────────────────
void Ui::press(const Hit& h, bool doubleClick) {
  switch (h.region) {
    case Region::Close: quit_ = true; break;
    case Region::Prev: actPrev(); break;
    case Region::Play: actPlay(); break;
    case Region::Pause: player_.togglePause(); break;
    case Region::Stop: player_.stop(); break;
    case Region::Next: actNext(); break;
    case Region::Eject: setStatus("ADD FILES: DROP THEM ON THE WINDOW OR PASS THEM ON THE COMMAND LINE", 3.0); break;
    case Region::Shuffle: key(Key::Shuffle); break;
    case Region::Repeat: key(Key::Repeat); break;
    case Region::EqToggle: showEq_ = !showEq_; break;
    case Region::PlToggle: showPl_ = !showPl_; break;
    case Region::Time: remaining_ = !remaining_; break;
    case Region::Seek:
      if (player_.hasTrack()) drag_ = Drag::Seek;
      break;
    case Region::Volume: drag_ = Drag::Volume; break;
    case Region::Balance: drag_ = Drag::Balance; break;
    case Region::EqOn: player_.setEqEnabled(!player_.eqEnabled()); break;
    case Region::EqPresets:
      setEqPreset((eqPreset_ + 1) % static_cast<int>(eqPresets().size()));
      break;
    case Region::EqPreamp: drag_ = Drag::EqPreamp; break;
    case Region::EqBand:
      drag_ = Drag::EqBand;
      dragIndex_ = h.index;
      break;
    case Region::PlRow:
      selected_ = h.index;
      if (doubleClick) playIndex(h.index);
      break;
    case Region::PlScroll: drag_ = Drag::PlScroll; break;
    case Region::PlRem: key(Key::DeleteSelected); break;
    case Region::PlClr:
      playlist_.clear();
      player_.stop();
      player_.close();
      selected_ = -1;
      scroll_ = 0;
      break;
    default: break;
  }
}

void Ui::mouseDown(int x, int y, bool doubleClick) {
  const Hit h = hitTest(x, y);
  pressed_ = h;
  press(h, doubleClick);
  if (drag_ != Drag::None) dragTo(x, y);
}

void Ui::mouseUp(int x, int y) {
  (void)x;
  (void)y;
  if (drag_ == Drag::Seek && player_.hasTrack()) player_.seek(dragSeek_ * player_.duration());
  drag_ = Drag::None;
  dragIndex_ = -1;
  pressed_ = {};
}

void Ui::mouseMove(int x, int y) {
  if (drag_ != Drag::None) dragTo(x, y);
}

void Ui::dragTo(int x, int y) {
  switch (drag_) {
    case Drag::Seek: dragSeek_ = sliderFrac(x, kSeek.x, kSeek.w, kSeekThumbW); break;
    case Drag::Volume: {
      const float v = sliderFrac(x, kVol.x, kVol.w, kSliderThumbW);
      player_.setVolume(v);
      setStatus("VOLUME: " + std::to_string(static_cast<int>(std::lround(v * 100))) + "%", 1.2);
      break;
    }
    case Drag::Balance: {
      float b = sliderFrac(x, kBal.x, kBal.w, kSliderThumbW) * 2.0f - 1.0f;
      if (std::fabs(b) < 0.12f) b = 0.0f;  // centre detent
      player_.setBalance(b);
      const int pct = static_cast<int>(std::lround(std::fabs(b) * 100));
      setStatus(b == 0.0f ? "BALANCE: CENTER" : "BALANCE: " + std::to_string(pct) + "% " + (b < 0 ? "LEFT" : "RIGHT"), 1.2);
      break;
    }
    case Drag::EqBand:
    case Drag::EqPreamp: {
      const int ly = y - eqTop();
      float db = (1.0f - 2.0f * sliderFrac(ly, kEqSliderY, kEqSliderH, kEqThumbH)) * kEqMaxDb;
      if (std::fabs(db) < 0.8f) db = 0.0f;  // centre detent
      if (drag_ == Drag::EqBand) player_.setEqGain(dragIndex_, db);
      else player_.setEqPreamp(db);
      break;
    }
    case Drag::PlScroll: {
      const int maxScroll = std::max(0, static_cast<int>(playlist_.size()) - visibleRows());
      const float f = sliderFrac(y - plTop(), kPlBar.y, kPlBar.h, 8);
      scroll_ = static_cast<int>(std::lround(f * static_cast<float>(maxScroll)));
      clampScroll();
      break;
    }
    case Drag::None: break;
  }
}

void Ui::wheel(int x, int y, int dy) {
  const Hit h = hitTest(x, y);
  if (h.region == Region::PlRow || h.region == Region::PlList || h.region == Region::PlScroll) {
    scroll_ -= dy * 3;
    clampScroll();
  } else if (h.region == Region::Volume || h.region == Region::Marquee || h.region == Region::Vis) {
    key(dy > 0 ? Key::VolUp : Key::VolDown);
  } else if (h.region == Region::Seek) {
    key(dy > 0 ? Key::SeekFwd : Key::SeekBack);
  }
}

// ── Per-frame update ────────────────────────────────────────────────────────
void Ui::tick(double dt) {
  clock_ += dt;
  if (statusT_ > 0) {
    statusT_ -= dt;
    if (statusT_ <= 0) status_.clear();
  }
  marquee_ += dt * 28.0;

  float buf[1024];
  if (player_.state() == PlayState::Playing) {
    player_.copyLatest(buf, 1024);
    analyzer_.process(buf, 1024, static_cast<float>(dt));
  } else {
    analyzer_.decay(static_cast<float>(dt));
  }

  if (player_.consumeEnded()) {
    const int next = playlist_.next(false);
    if (next >= 0) playIndex(next);
    else player_.stop();
  }
}

// ── Rendering ───────────────────────────────────────────────────────────────
void Ui::render(Canvas& out) const {
  if (out.width() != kWidth || out.height() != windowHeight()) out.resize(kWidth, windowHeight());
  Canvas main(kWidth, kMainH);
  drawMain(main);
  out.blit(main, 0, 0);
  int y = kMainH;
  if (showEq_) {
    Canvas eq(kWidth, kEqH);
    drawEq(eq);
    out.blit(eq, 0, y);
    y += kEqH;
  }
  if (showPl_) {
    Canvas pl(kWidth, kPlH);
    drawPlaylist(pl);
    out.blit(pl, 0, y);
  }
}

void Ui::drawMain(Canvas& c) const {
  const Theme& t = theme_;
  windowFace(c, t);
  titleBar(c, t, "WENAMP", true);

  const bool hasTrack = player_.hasTrack();
  const PlayState st = player_.state();
  const double dur = player_.duration();

  // ── play-state glyph + time ──
  well(c, t, {kTime.x - 12, kTime.y, kTime.w + 12, kTime.h});
  {
    const int gx = 26, gy = 28;
    if (st == PlayState::Playing) triRight(c, gx, gy, 9, t.lcdOn);
    else if (st == PlayState::Paused) {
      c.fillRect(gx, gy, 3, 9, t.lcdOn);
      c.fillRect(gx + 5, gy, 3, 9, t.lcdOn);
    } else {
      c.fillRect(gx, gy + 1, 8, 8, hasTrack ? t.lcdOn : t.lcdDim);
    }
  }
  {
    double shown = player_.position();
    if (drag_ == Drag::Seek) shown = dragSeek_ * dur;
    if (remaining_ && hasTrack) shown = std::max(0.0, dur - shown);
    const std::string s = mmss(shown);  // "MM:SS"
    const bool blinkOff = st == PlayState::Paused && std::fmod(clock_, 1.0) > 0.5;
    const bool lit = !blinkOff;
    if (remaining_ && hasTrack) c.fillRect(kTime.x + 2, kTime.y + 8, 6, 2, t.lcdOn);
    const int dx[4] = {48, 60, 78, 90};
    const int src[4] = {0, 1, 3, 4};
    for (int i = 0; i < 4; ++i) segDigit(c, t, dx[i], kTime.y + 2, s[static_cast<std::size_t>(src[i])] - '0', lit);
    c.fillRect(72, kTime.y + 5, 2, 2, lit ? t.lcdOn : t.lcdDim);
    c.fillRect(72, kTime.y + 10, 2, 2, lit ? t.lcdOn : t.lcdDim);
  }

  // ── visualizer ──
  well(c, t, kVis);
  c.hline(kVis.x, kVis.y + kVis.h - 1, kVis.w, t.lcdDim);
  for (int b = 0; b < SpectrumAnalyzer::kBars; ++b) {
    const int h = static_cast<int>(std::lround(analyzer_.bars()[static_cast<std::size_t>(b)] * (kVis.h - 1)));
    const int x = kVis.x + b * 4;
    for (int r = 0; r < h; ++r) c.hline(x, kVis.y + kVis.h - 2 - r, 3, barColor(t, r, kVis.h - 1));
    const float pk = analyzer_.peaks()[static_cast<std::size_t>(b)];
    if (pk > 0.04f) {
      const int py = kVis.y + kVis.h - 2 - static_cast<int>(std::lround(pk * (kVis.h - 2)));
      c.hline(x, py, 3, t.peak);
    }
  }

  // ── title marquee ──
  {
    well(c, t, kMarq);
    Canvas strip(kMarq.w, kMarq.h);
    strip.clear(t.well);
    const std::string title = titleLine();
    const int tw = Canvas::textWidth(title);
    if (tw <= kMarq.w - 2) {
      strip.text(2, 1, title, t.lcdOn);
    } else {
      const std::string gap = "   ***   ";
      const int period = Canvas::textWidth(title + gap) + 1;
      const int off = static_cast<int>(marquee_) % period;
      strip.text(2 - off, 1, title + gap, t.lcdOn);
      strip.text(2 - off + period, 1, title + gap, t.lcdOn);
    }
    c.blit(strip, kMarq.x, kMarq.y);
  }

  // ── kbps / kHz / mono-stereo ──
  {
    well(c, t, kInfo);
    const TrackInfo ti = player_.info();
    const std::uint32_t on = hasTrack ? t.lcdOn : t.lcdDim;
    const std::string kbps = hasTrack && ti.bitrateKbps ? std::to_string(ti.bitrateKbps) : "---";
    const std::string khz = hasTrack && ti.sourceRate ? std::to_string(ti.sourceRate / 1000) : "--";
    c.text(kInfo.x + 3, kInfo.y + 4, kbps, on);
    c.text(kInfo.x + 3 + Canvas::textWidth(kbps) + 3, kInfo.y + 4, "kbps", t.lcdDim);
    c.text(kInfo.x + 56, kInfo.y + 4, khz, on);
    c.text(kInfo.x + 56 + Canvas::textWidth(khz) + 3, kInfo.y + 4, "kHz", t.lcdDim);
    c.text(kInfo.x + 94, kInfo.y + 4, "MONO", hasTrack && ti.sourceChannels == 1 ? t.lcdOn : t.lcdDim);
    c.text(kInfo.x + 120, kInfo.y + 4, "STEREO", hasTrack && ti.sourceChannels >= 2 ? t.lcdOn : t.lcdDim);
  }

  // ── volume / balance ──
  hslider(c, t, kVol, player_.volume(), kSliderThumbW, true);
  hslider(c, t, kBal, (player_.balance() + 1.0f) / 2.0f, kSliderThumbW, false);

  // ── EQ / PL toggles ──
  button(c, t, kEqBtn, pressed_.region == Region::EqToggle, showEq_);
  textCentered(c, kEqBtn, "EQ", showEq_ ? t.titleText : t.buttonIcon);
  button(c, t, kPlBtn, pressed_.region == Region::PlToggle, showPl_);
  textCentered(c, kPlBtn, "PL", showPl_ ? t.titleText : t.buttonIcon);

  // ── seek bar ──
  {
    const int ty = kSeek.y + 3;
    c.fillRect(kSeek.x, ty, kSeek.w, 4, t.sliderTrack);
    c.bevel(kSeek.x - 1, ty - 1, kSeek.w + 2, 6, t.bodyLo, t.bodyHi);
    if (hasTrack && dur > 0) {
      double frac = drag_ == Drag::Seek ? dragSeek_ : player_.position() / dur;
      frac = std::clamp(frac, 0.0, 1.0);
      const int tx = kSeek.x + static_cast<int>(std::lround(frac * (kSeek.w - kSeekThumbW)));
      c.fillRect(kSeek.x, ty, tx - kSeek.x, 4, mix(t.lcdDim, t.lcdOn, 0.55f));
      c.fillRect(tx, kSeek.y, kSeekThumbW, kSeek.h, t.sliderThumb);
      c.bevel(tx, kSeek.y, kSeekThumbW, kSeek.h, t.bodyHi, t.bodyLo);
      c.vline(tx + kSeekThumbW / 2, kSeek.y + 2, kSeek.h - 4, t.bodyLo);
    }
  }

  // ── transport ──
  const std::uint32_t ic = t.buttonIcon;
  button(c, t, kPrev, pressed_.region == Region::Prev);
  c.vline(kPrev.x + 7, kPrev.y + 4, 9, ic);
  triLeft(c, kPrev.x + 9, kPrev.y + 4, 9, ic);
  button(c, t, kPlay, pressed_.region == Region::Play, st == PlayState::Playing);
  triRight(c, kPlay.x + 9, kPlay.y + 4, 9, ic);
  button(c, t, kPause, pressed_.region == Region::Pause, st == PlayState::Paused);
  c.fillRect(kPause.x + 7, kPause.y + 4, 3, 9, ic);
  c.fillRect(kPause.x + 13, kPause.y + 4, 3, 9, ic);
  button(c, t, kStop, pressed_.region == Region::Stop);
  c.fillRect(kStop.x + 7, kStop.y + 5, 9, 8, ic);
  button(c, t, kNext, pressed_.region == Region::Next);
  triRight(c, kNext.x + 5, kNext.y + 4, 9, ic);
  c.vline(kNext.x + 14, kNext.y + 4, 9, ic);
  c.vline(kNext.x + 15, kNext.y + 4, 9, ic);
  button(c, t, kEject, pressed_.region == Region::Eject);
  for (int i = 0; i < 5; ++i) c.hline(kEject.x + 7 + (4 - i), kEject.y + 3 + i, 1 + 2 * i, ic);
  c.fillRect(kEject.x + 6, kEject.y + 10, 11, 2, ic);

  button(c, t, kShuffle, pressed_.region == Region::Shuffle, playlist_.shuffle());
  textCentered(c, kShuffle, "SHUFFLE", playlist_.shuffle() ? t.titleText : t.buttonIcon);
  const RepeatMode rm = playlist_.repeat();
  button(c, t, kRepeat, pressed_.region == Region::Repeat, rm != RepeatMode::Off);
  textCentered(c, kRepeat, rm == RepeatMode::One ? "REPEAT 1" : "REPEAT", rm != RepeatMode::Off ? t.titleText : t.buttonIcon);

  // ── status strip ──
  if (!status_.empty()) c.text(kStatus.x, kStatus.y, status_, t.accent);
}

void Ui::drawEq(Canvas& c) const {
  const Theme& t = theme_;
  windowFace(c, t);
  titleBar(c, t, "WENAMP EQUALIZER", true);

  button(c, t, kEqOn, pressed_.region == Region::EqOn, player_.eqEnabled());
  textCentered(c, kEqOn, "ON", player_.eqEnabled() ? t.titleText : t.buttonIcon);
  button(c, t, kEqPre, pressed_.region == Region::EqPresets);
  std::string pname = eqPresets()[static_cast<std::size_t>(eqPreset_)].name;
  if (pname.size() > 9) pname.resize(9);
  textCentered(c, kEqPre, pname, t.buttonIcon);

  // response curve
  well(c, t, kEqGraph);
  const int mid = kEqGraph.y + kEqGraph.h / 2;
  c.hline(kEqGraph.x, mid, kEqGraph.w, t.lcdDim);
  std::vector<float> freqs;
  for (int i = 0; i < kEqGraph.w; ++i)
    freqs.push_back(static_cast<float>(30.0 * std::pow(20000.0 / 30.0, static_cast<double>(i) / (kEqGraph.w - 1))));
  const std::vector<float> resp = player_.eqResponse(freqs);
  int prev = mid;
  for (int i = 0; i < kEqGraph.w; ++i) {
    const float db = player_.eqEnabled() ? resp[static_cast<std::size_t>(i)] : 0.0f;
    int y = mid - static_cast<int>(std::lround(db / kEqMaxDb * (kEqGraph.h / 2 - 1)));
    y = clampi(y, kEqGraph.y, kEqGraph.y + kEqGraph.h - 1);
    const int lo = std::min(prev, y), hi = std::max(prev, y);
    for (int yy = lo; yy <= hi; ++yy) c.set(kEqGraph.x + i, yy, t.lcdOn);
    prev = y;
  }

  // dB scale
  c.text(48, kEqSliderY - 3, "+12", t.lcdDim);
  c.text(48, kEqSliderY + kEqSliderH / 2 - 4, "0", t.lcdDim);
  c.text(48, kEqSliderY + kEqSliderH - 6, "-12", t.lcdDim);

  const auto topFrac = [](float db) { return (1.0f - db / kEqMaxDb) / 2.0f; };
  vslider(c, t, kEqPreX, kEqSliderY, kEqSliderH, kEqSliderW, topFrac(player_.eqPreamp()));
  c.text(kEqPreX - 4, kEqSliderY + kEqSliderH + 4, "PRE", t.lcdDim);
  for (int i = 0; i < kEqBands; ++i) {
    const int sx = kEqBandX0 + i * kEqBandStep;
    vslider(c, t, sx, kEqSliderY, kEqSliderH, kEqSliderW, topFrac(player_.eqGain(i)));
    const std::string lab = kEqLabels[static_cast<std::size_t>(i)];
    // labels alternate between two rows so 3-digit ones never touch
    c.text(sx + (kEqSliderW - Canvas::textWidth(lab)) / 2, kEqSliderY + kEqSliderH + 4 + (i % 2) * 8, lab, t.lcdDim);
  }
}

void Ui::drawPlaylist(Canvas& c) const {
  const Theme& t = theme_;
  windowFace(c, t);
  titleBar(c, t, "WENAMP PLAYLIST", true);

  c.fillRect(kPlList.x, kPlList.y, kPlList.w, kPlList.h, t.listBg);
  c.bevel(kPlList.x - 1, kPlList.y - 1, kPlList.w + 2, kPlList.h + 2, t.bodyLo, t.bodyHi);
  const int rows = visibleRows();
  for (int r = 0; r < rows; ++r) {
    const int idx = scroll_ + r;
    if (idx >= static_cast<int>(playlist_.size())) break;
    const int y = kPlList.y + r * kRowH;
    if (idx == selected_) c.fillRect(kPlList.x, y, kPlList.w, kRowH, t.listSel);
    std::string line = playlist_.displayTitle(static_cast<std::size_t>(idx));
    const std::size_t maxChars = static_cast<std::size_t>((kPlList.w - 4) / 6);
    if (line.size() > maxChars) line = line.substr(0, maxChars - 1) + "~";
    const bool current = idx == playlist_.current();
    c.text(kPlList.x + 2, y + 1, line, current ? t.accent : t.listText);
  }
  if (playlist_.empty()) c.text(kPlList.x + 4, kPlList.y + 4, "(EMPTY) DROP FILES ON THE WINDOW", t.lcdDim);

  // scrollbar
  c.fillRect(kPlBar.x, kPlBar.y, kPlBar.w, kPlBar.h, t.sliderTrack);
  c.bevel(kPlBar.x - 1, kPlBar.y - 1, kPlBar.w + 2, kPlBar.h + 2, t.bodyLo, t.bodyHi);
  const int n = static_cast<int>(playlist_.size());
  if (n > rows) {
    const int th = std::max(8, kPlBar.h * rows / n);
    const int maxScroll = n - rows;
    const int ty = kPlBar.y + (kPlBar.h - th) * scroll_ / std::max(1, maxScroll);
    c.fillRect(kPlBar.x, ty, kPlBar.w, th, t.sliderThumb);
    c.bevel(kPlBar.x, ty, kPlBar.w, th, t.bodyHi, t.bodyLo);
  }

  button(c, t, kPlRem, pressed_.region == Region::PlRem);
  textCentered(c, kPlRem, "REM", t.buttonIcon);
  button(c, t, kPlClr, pressed_.region == Region::PlClr);
  textCentered(c, kPlClr, "CLR", t.buttonIcon);

  const std::string total = std::to_string(n) + (n == 1 ? " TRACK  " : " TRACKS  ") + Playlist::formatTime(playlist_.totalDuration());
  c.text(kPlList.x + kPlList.w - Canvas::textWidth(total), kPlRem.y + 3, total, t.lcdOn);
}

}  // namespace wenamp
