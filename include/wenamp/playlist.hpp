#pragma once
#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace wenamp {

struct Entry {
  std::string path;
  std::string title;      // "Artist - Title" from tags, else the file stem
  double duration = -1.0; // seconds, -1 = not known yet
};

enum class RepeatMode { Off, All, One };

class Playlist {
 public:
  explicit Playlist(std::uint32_t seed = 0x5EED);

  std::size_t size() const { return entries_.size(); }
  bool empty() const { return entries_.empty(); }
  const Entry& at(std::size_t i) const { return entries_.at(i); }
  Entry& at(std::size_t i) { return entries_.at(i); }

  // Adds a file, reading ID3 tags / the file stem for its title. Returns its index.
  std::size_t addPath(const std::string& path);
  std::size_t add(Entry e);
  void remove(std::size_t i);
  void clear();
  void move(std::size_t from, std::size_t to);

  int current() const { return current_; }
  bool setCurrent(std::size_t i);

  // Auto-advance (end of track) honours RepeatMode::One; user-initiated does not.
  // Return the new index, or -1 when playback should stop. Both update current().
  int next(bool userInitiated = false);
  int previous();

  void setShuffle(bool on);
  bool shuffle() const { return shuffle_; }
  void setRepeat(RepeatMode m) { repeat_ = m; }
  RepeatMode repeat() const { return repeat_; }
  void cycleRepeat();

  bool loadM3U(const std::string& path, std::string* err = nullptr);
  bool saveM3U(const std::string& path, std::string* err = nullptr) const;

  double totalDuration() const;  // sum of known durations
  std::string displayTitle(std::size_t i) const;  // "3. Artist - Title (3:41)"

  static std::string formatTime(double seconds);  // m:ss (h:mm:ss when long)
  static std::string stemOf(const std::string& path);

 private:
  void rebuildOrder();

  std::vector<Entry> entries_;
  int current_ = -1;
  bool shuffle_ = false;
  RepeatMode repeat_ = RepeatMode::Off;
  std::vector<std::size_t> order_;  // shuffle permutation
  std::size_t orderPos_ = 0;
  std::mt19937 rng_;
};

}  // namespace wenamp
