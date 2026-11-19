#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace wenamp {

constexpr std::uint32_t rgb(unsigned r, unsigned g, unsigned b) {
  return 0xFF000000u | (r << 16) | (g << 8) | b;
}
constexpr std::uint32_t mix(std::uint32_t a, std::uint32_t b, float t) {
  const auto ch = [&](int sh) {
    const float x = static_cast<float>((a >> sh) & 0xFF);
    const float y = static_cast<float>((b >> sh) & 0xFF);
    return static_cast<std::uint32_t>(x + (y - x) * t + 0.5f) & 0xFF;
  };
  return 0xFF000000u | (ch(16) << 16) | (ch(8) << 8) | ch(0);
}

// A tiny software framebuffer (ARGB32) with just the primitives the skin needs.
class Canvas {
 public:
  Canvas(int w = 0, int h = 0) { resize(w, h); }
  void resize(int w, int h);

  int width() const { return w_; }
  int height() const { return h_; }
  const std::uint32_t* data() const { return px_.data(); }
  std::uint32_t pixel(int x, int y) const;

  void clear(std::uint32_t c);
  void set(int x, int y, std::uint32_t c);  // clipped
  void fillRect(int x, int y, int w, int h, std::uint32_t c);
  void rect(int x, int y, int w, int h, std::uint32_t c);  // 1px outline
  void hline(int x, int y, int w, std::uint32_t c) { fillRect(x, y, w, 1, c); }
  void vline(int x, int y, int h, std::uint32_t c) { fillRect(x, y, 1, h, c); }
  // 1px raised (or sunken) bevel border: light top/left, dark bottom/right.
  void bevel(int x, int y, int w, int h, std::uint32_t hi, std::uint32_t lo);
  void blit(const Canvas& src, int dx, int dy);
  void blitRegion(const Canvas& src, int sy, int sh, int dx, int dy);  // rows [sy, sy+sh)

  // 5x7 bitmap font, 6 px advance. Returns the x just past the last glyph.
  int text(int x, int y, const std::string& s, std::uint32_t c);
  static int textWidth(const std::string& s) { return s.empty() ? 0 : static_cast<int>(s.size()) * 6 - 1; }

  // Nearest-neighbour integer upscale (for screenshots).
  Canvas scaled(int k) const;
  bool writePPM(const std::string& path) const;
  std::uint64_t hash() const;  // FNV-1a over the pixels, for deterministic render tests

 private:
  int w_ = 0, h_ = 0;
  std::vector<std::uint32_t> px_;
};

}  // namespace wenamp
