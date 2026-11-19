#include "wenamp/canvas.hpp"

#include <algorithm>
#include <cstdio>

#include "wenamp/font.hpp"

namespace wenamp {

void Canvas::resize(int w, int h) {
  w_ = std::max(0, w);
  h_ = std::max(0, h);
  px_.assign(static_cast<std::size_t>(w_) * static_cast<std::size_t>(h_), 0xFF000000u);
}

std::uint32_t Canvas::pixel(int x, int y) const {
  if (x < 0 || y < 0 || x >= w_ || y >= h_) return 0;
  return px_[static_cast<std::size_t>(y) * w_ + x];
}

void Canvas::clear(std::uint32_t c) { std::fill(px_.begin(), px_.end(), c); }

void Canvas::set(int x, int y, std::uint32_t c) {
  if (x < 0 || y < 0 || x >= w_ || y >= h_) return;
  px_[static_cast<std::size_t>(y) * w_ + x] = c;
}

void Canvas::fillRect(int x, int y, int w, int h, std::uint32_t c) {
  const int x0 = std::max(0, x), y0 = std::max(0, y);
  const int x1 = std::min(w_, x + w), y1 = std::min(h_, y + h);
  for (int j = y0; j < y1; ++j)
    for (int i = x0; i < x1; ++i) px_[static_cast<std::size_t>(j) * w_ + i] = c;
}

void Canvas::rect(int x, int y, int w, int h, std::uint32_t c) {
  if (w <= 0 || h <= 0) return;
  hline(x, y, w, c);
  hline(x, y + h - 1, w, c);
  vline(x, y, h, c);
  vline(x + w - 1, y, h, c);
}

void Canvas::bevel(int x, int y, int w, int h, std::uint32_t hi, std::uint32_t lo) {
  if (w <= 0 || h <= 0) return;
  hline(x, y, w, hi);
  vline(x, y, h, hi);
  hline(x, y + h - 1, w, lo);
  vline(x + w - 1, y, h, lo);
}

void Canvas::blit(const Canvas& s, int dx, int dy) { blitRegion(s, 0, s.h_, dx, dy); }

void Canvas::blitRegion(const Canvas& s, int sy, int sh, int dx, int dy) {
  for (int j = 0; j < sh; ++j)
    for (int i = 0; i < s.w_; ++i) set(dx + i, dy + j, s.pixel(i, sy + j));
}

int Canvas::text(int x, int y, const std::string& str, std::uint32_t c) {
  for (unsigned char ch : str) {
    const std::uint8_t* g = glyph5x7(ch);
    for (int col = 0; col < 5; ++col)
      for (int row = 0; row < 7; ++row)
        if ((g[col] >> row) & 1) set(x + col, y + row, c);
    x += 6;
  }
  return x;
}

Canvas Canvas::scaled(int k) const {
  Canvas out(w_ * k, h_ * k);
  for (int y = 0; y < out.h_; ++y)
    for (int x = 0; x < out.w_; ++x) out.px_[static_cast<std::size_t>(y) * out.w_ + x] = pixel(x / k, y / k);
  return out;
}

bool Canvas::writePPM(const std::string& path) const {
  std::FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  std::fprintf(f, "P6\n%d %d\n255\n", w_, h_);
  for (std::uint32_t p : px_) {
    const unsigned char rgbv[3] = {static_cast<unsigned char>((p >> 16) & 0xFF),
                                   static_cast<unsigned char>((p >> 8) & 0xFF),
                                   static_cast<unsigned char>(p & 0xFF)};
    std::fwrite(rgbv, 1, 3, f);
  }
  return std::fclose(f) == 0;
}

std::uint64_t Canvas::hash() const {
  std::uint64_t h = 1469598103934665603ull;
  for (std::uint32_t p : px_) {
    for (int s = 0; s < 32; s += 8) {
      h ^= (p >> s) & 0xFF;
      h *= 1099511628211ull;
    }
  }
  return h;
}

}  // namespace wenamp
