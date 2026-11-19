#include "wenamp/playlist.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <sstream>

#include "wenamp/tags.hpp"

namespace fs = std::filesystem;

namespace wenamp {

Playlist::Playlist(std::uint32_t seed) : rng_(seed) {}

std::string Playlist::stemOf(const std::string& path) {
  return fs::path(path).stem().string();
}

std::string Playlist::formatTime(double seconds) {
  if (seconds < 0 || std::isnan(seconds)) return "--:--";
  const long total = static_cast<long>(seconds + 0.5);
  const long h = total / 3600, m = (total / 60) % 60, s = total % 60;
  char buf[32];
  if (h > 0) std::snprintf(buf, sizeof buf, "%ld:%02ld:%02ld", h, m, s);
  else std::snprintf(buf, sizeof buf, "%ld:%02ld", total / 60, s);
  return buf;
}

std::size_t Playlist::addPath(const std::string& path) {
  Entry e;
  e.path = path;
  Tags t;
  if (readTags(path, t) && !t.title.empty()) {
    e.title = t.artist.empty() ? t.title : t.artist + " - " + t.title;
  } else {
    e.title = stemOf(path);
  }
  return add(std::move(e));
}

std::size_t Playlist::add(Entry e) {
  entries_.push_back(std::move(e));
  if (shuffle_) rebuildOrder();
  return entries_.size() - 1;
}

void Playlist::remove(std::size_t i) {
  if (i >= entries_.size()) return;
  entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(i));
  if (current_ >= 0) {
    if (static_cast<std::size_t>(current_) == i) current_ = -1;
    else if (static_cast<std::size_t>(current_) > i) --current_;
  }
  if (shuffle_) rebuildOrder();
}

void Playlist::clear() {
  entries_.clear();
  order_.clear();
  current_ = -1;
  orderPos_ = 0;
}

void Playlist::move(std::size_t from, std::size_t to) {
  if (from >= entries_.size() || to >= entries_.size() || from == to) return;
  Entry e = std::move(entries_[from]);
  entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(from));
  entries_.insert(entries_.begin() + static_cast<std::ptrdiff_t>(to), std::move(e));
  if (current_ >= 0) {
    const std::size_t c = static_cast<std::size_t>(current_);
    if (c == from) current_ = static_cast<int>(to);
    else if (from < c && to >= c) --current_;
    else if (from > c && to <= c) ++current_;
  }
  if (shuffle_) rebuildOrder();
}

bool Playlist::setCurrent(std::size_t i) {
  if (i >= entries_.size()) return false;
  current_ = static_cast<int>(i);
  if (shuffle_) {
    const auto it = std::find(order_.begin(), order_.end(), i);
    if (it != order_.end()) orderPos_ = static_cast<std::size_t>(it - order_.begin());
  }
  return true;
}

void Playlist::rebuildOrder() {
  order_.resize(entries_.size());
  std::iota(order_.begin(), order_.end(), std::size_t{0});
  std::shuffle(order_.begin(), order_.end(), rng_);
  orderPos_ = 0;
  if (current_ >= 0) {
    // Keep the playing track first so "next" never replays it immediately.
    const auto it = std::find(order_.begin(), order_.end(), static_cast<std::size_t>(current_));
    if (it != order_.end()) std::iter_swap(order_.begin(), it);
  }
}

void Playlist::setShuffle(bool on) {
  shuffle_ = on;
  if (on) rebuildOrder();
}

void Playlist::cycleRepeat() {
  repeat_ = repeat_ == RepeatMode::Off ? RepeatMode::All
            : repeat_ == RepeatMode::All ? RepeatMode::One
                                         : RepeatMode::Off;
}

int Playlist::next(bool userInitiated) {
  if (entries_.empty()) return -1;
  if (!userInitiated && repeat_ == RepeatMode::One && current_ >= 0) return current_;

  if (shuffle_) {
    if (order_.size() != entries_.size()) rebuildOrder();
    if (orderPos_ + 1 < order_.size()) {
      ++orderPos_;
    } else if (repeat_ == RepeatMode::All || userInitiated) {
      // rebuildOrder() keeps the track that just played at position 0, so
      // stepping to position 1 can never replay it across the reshuffle.
      rebuildOrder();
      orderPos_ = order_.size() > 1 ? 1 : 0;
    } else {
      return -1;
    }
    current_ = static_cast<int>(order_[orderPos_]);
    return current_;
  }

  if (current_ < 0) current_ = 0;
  else if (static_cast<std::size_t>(current_) + 1 < entries_.size()) ++current_;
  else if (repeat_ == RepeatMode::All || userInitiated) current_ = 0;
  else return -1;
  return current_;
}

int Playlist::previous() {
  if (entries_.empty()) return -1;
  if (shuffle_) {
    if (order_.size() != entries_.size()) rebuildOrder();
    if (orderPos_ > 0) --orderPos_;
    current_ = static_cast<int>(order_[orderPos_]);
    return current_;
  }
  if (current_ <= 0) current_ = repeat_ == RepeatMode::All ? static_cast<int>(entries_.size()) - 1 : 0;
  else --current_;
  return current_;
}

double Playlist::totalDuration() const {
  double t = 0;
  for (const auto& e : entries_)
    if (e.duration > 0) t += e.duration;
  return t;
}

std::string Playlist::displayTitle(std::size_t i) const {
  const Entry& e = entries_.at(i);
  std::string s = std::to_string(i + 1) + ". " + e.title;
  if (e.duration >= 0) s += " (" + formatTime(e.duration) + ")";
  return s;
}

bool Playlist::loadM3U(const std::string& path, std::string* err) {
  std::ifstream f(path);
  if (!f) {
    if (err) *err = "cannot open " + path;
    return false;
  }
  const fs::path base = fs::path(path).parent_path();
  std::string line;
  double pendingDur = -1;
  std::string pendingTitle;
  while (std::getline(f, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
    if (line.empty() || line == "#EXTM3U") continue;
    if (line.rfind("#EXTINF:", 0) == 0) {
      const std::string rest = line.substr(8);
      const auto comma = rest.find(',');
      try {
        pendingDur = std::stod(rest.substr(0, comma));
      } catch (...) {
        pendingDur = -1;
      }
      pendingTitle = comma == std::string::npos ? "" : rest.substr(comma + 1);
      continue;
    }
    if (line[0] == '#') continue;
    fs::path p(line);
    if (p.is_relative()) p = base / p;
    Entry e;
    e.path = p.lexically_normal().string();
    e.title = pendingTitle.empty() ? stemOf(e.path) : pendingTitle;
    e.duration = pendingDur;
    add(std::move(e));
    pendingDur = -1;
    pendingTitle.clear();
  }
  return true;
}

bool Playlist::saveM3U(const std::string& path, std::string* err) const {
  std::ofstream f(path);
  if (!f) {
    if (err) *err = "cannot write " + path;
    return false;
  }
  f << "#EXTM3U\n";
  for (const auto& e : entries_) {
    f << "#EXTINF:" << (e.duration >= 0 ? static_cast<long>(std::lround(e.duration)) : -1) << ','
      << e.title << '\n'
      << e.path << '\n';
  }
  return static_cast<bool>(f);
}

}  // namespace wenamp
