#include <algorithm>
#include <fstream>
#include <set>
#include <vector>

#include "testing.hpp"
#include "wenamp/playlist.hpp"

using namespace wenamp;

namespace {
Playlist makeList(int n, std::uint32_t seed = 0x5EED) {
  Playlist p(seed);
  for (int i = 0; i < n; ++i) {
    Entry e;
    e.path = "/music/track" + std::to_string(i) + ".mp3";
    e.title = "Track " + std::to_string(i);
    p.add(e);
  }
  return p;
}
}  // namespace

TEST(playlist_time_formatting) {
  CHECK_EQ(Playlist::formatTime(0), std::string("0:00"));
  CHECK_EQ(Playlist::formatTime(59.4), std::string("0:59"));
  CHECK_EQ(Playlist::formatTime(61), std::string("1:01"));
  CHECK_EQ(Playlist::formatTime(3725), std::string("1:02:05"));
  CHECK_EQ(Playlist::formatTime(-1), std::string("--:--"));
}

TEST(playlist_stem_and_display_title) {
  CHECK_EQ(Playlist::stemOf("/a/b/Some Song.flac"), std::string("Some Song"));
  Playlist p;
  Entry e;
  e.path = "x.mp3";
  e.title = "Foo";
  e.duration = 221;
  p.add(e);
  CHECK_EQ(p.displayTitle(0), std::string("1. Foo (3:41)"));
  p.at(0).duration = -1;
  CHECK_EQ(p.displayTitle(0), std::string("1. Foo"));
}

TEST(playlist_linear_navigation_and_repeat_all) {
  Playlist p = makeList(3);
  CHECK(p.setCurrent(0));
  CHECK_EQ(p.next(), 1);
  CHECK_EQ(p.next(), 2);
  CHECK_EQ(p.next(), -1);  // end, repeat off
  p.setRepeat(RepeatMode::All);
  CHECK_EQ(p.next(), 0);   // wraps
  CHECK_EQ(p.previous(), 2);  // wraps back under repeat-all
  p.setRepeat(RepeatMode::Off);
  p.setCurrent(0);
  CHECK_EQ(p.previous(), 0);  // clamps at the start
}

TEST(playlist_repeat_one_only_applies_to_auto_advance) {
  Playlist p = makeList(3);
  p.setRepeat(RepeatMode::One);
  p.setCurrent(1);
  CHECK_EQ(p.next(false), 1);  // track ended: replay it
  CHECK_EQ(p.next(true), 2);   // user pressed Next: move on
}

TEST(playlist_user_next_wraps_even_with_repeat_off) {
  Playlist p = makeList(2);
  p.setCurrent(1);
  CHECK_EQ(p.next(true), 0);
}

TEST(playlist_cycle_repeat_modes) {
  Playlist p;
  CHECK(p.repeat() == RepeatMode::Off);
  p.cycleRepeat();
  CHECK(p.repeat() == RepeatMode::All);
  p.cycleRepeat();
  CHECK(p.repeat() == RepeatMode::One);
  p.cycleRepeat();
  CHECK(p.repeat() == RepeatMode::Off);
}

TEST(playlist_shuffle_visits_every_track_once) {
  Playlist p = makeList(8);
  p.setShuffle(true);
  p.setCurrent(3);
  std::set<int> seen{3};
  int steps = 0;
  for (int i = p.next(); i >= 0 && steps < 20; i = p.next(), ++steps) {
    CHECK(seen.insert(i).second);  // never repeats within a cycle
  }
  CHECK_EQ(seen.size(), static_cast<std::size_t>(8));
}

TEST(playlist_shuffle_is_deterministic_per_seed) {
  auto run = [](std::uint32_t seed) {
    Playlist p = makeList(6, seed);
    p.setShuffle(true);
    p.setCurrent(0);
    std::vector<int> seq;
    for (int i = 0; i < 5; ++i) seq.push_back(p.next(true));
    return seq;
  };
  CHECK(run(7) == run(7));
  CHECK(run(7) != run(8));
}

TEST(playlist_shuffle_repeat_all_reshuffles_without_immediate_repeat) {
  Playlist p = makeList(4);
  p.setShuffle(true);
  p.setRepeat(RepeatMode::All);
  p.setCurrent(0);
  int last = 0;
  for (int i = 0; i < 12; ++i) {
    const int n = p.next();
    CHECK(n >= 0);
    CHECK(n != last);
    last = n;
  }
}

TEST(playlist_previous_in_shuffle_walks_history) {
  Playlist p = makeList(6);
  p.setShuffle(true);
  p.setCurrent(2);
  const int a = p.next(true);
  const int b = p.next(true);
  CHECK_EQ(p.previous(), a);
  CHECK(b != a);
}

TEST(playlist_remove_keeps_current_consistent) {
  Playlist p = makeList(5);
  p.setCurrent(3);
  p.remove(1);
  CHECK_EQ(p.current(), 2);  // shifted down
  p.remove(2);               // remove the playing one
  CHECK_EQ(p.current(), -1);
  CHECK_EQ(p.size(), static_cast<std::size_t>(3));
}

TEST(playlist_move_tracks_the_current_entry) {
  Playlist p = makeList(5);
  p.setCurrent(1);
  p.move(1, 3);
  CHECK_EQ(p.current(), 3);
  CHECK_EQ(p.at(3).title, std::string("Track 1"));
  p.setCurrent(2);
  p.move(0, 4);  // an entry before current moves behind it
  CHECK_EQ(p.at(p.current()).title, std::string("Track 3"));
}

TEST(playlist_total_duration_ignores_unknowns) {
  Playlist p = makeList(3);
  p.at(0).duration = 60;
  p.at(2).duration = 30;
  CHECK_NEAR(p.totalDuration(), 90.0, 1e-9);
}

TEST(playlist_m3u_roundtrip) {
  Playlist p = makeList(3);
  p.at(0).duration = 180;
  p.at(1).title = "Artist - Name, With Comma";
  const auto f = (testing::scratch() / "roundtrip.m3u").string();
  CHECK(p.saveM3U(f));
  Playlist q;
  CHECK(q.loadM3U(f));
  CHECK_EQ(q.size(), static_cast<std::size_t>(3));
  CHECK_EQ(q.at(0).path, p.at(0).path);
  CHECK_NEAR(q.at(0).duration, 180.0, 1e-9);
  CHECK_EQ(q.at(1).title, p.at(1).title);
  CHECK_NEAR(q.at(2).duration, -1.0, 1e-9);
}

TEST(playlist_m3u_resolves_relative_paths_and_tolerates_plain_lists) {
  const auto dir = testing::scratch() / "rel";
  std::filesystem::create_directories(dir);
  {
    std::ofstream f(dir / "plain.m3u");
    f << "# a comment\nsub/one.mp3\r\n\r\ntwo.flac\n";
  }
  Playlist p;
  CHECK(p.loadM3U((dir / "plain.m3u").string()));
  CHECK_EQ(p.size(), static_cast<std::size_t>(2));
  CHECK_EQ(p.at(0).path, (dir / "sub" / "one.mp3").lexically_normal().string());
  CHECK_EQ(p.at(1).title, std::string("two"));
  CHECK(!p.loadM3U((dir / "missing.m3u").string()));
}
