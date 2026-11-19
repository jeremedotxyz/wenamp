#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "testing.hpp"
#include "wenamp/playlist.hpp"
#include "wenamp/tags.hpp"

using namespace wenamp;

namespace {

void writeFile(const std::filesystem::path& p, const std::vector<unsigned char>& bytes) {
  std::ofstream f(p, std::ios::binary);
  f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void pad(std::vector<unsigned char>& v, const std::string& s, std::size_t n) {
  for (std::size_t i = 0; i < n; ++i) v.push_back(i < s.size() ? static_cast<unsigned char>(s[i]) : 0);
}

std::vector<unsigned char> frame(const std::string& id, const std::vector<unsigned char>& payload) {
  std::vector<unsigned char> f(id.begin(), id.end());
  const std::uint32_t n = static_cast<std::uint32_t>(payload.size());
  f.push_back(static_cast<unsigned char>(n >> 24));
  f.push_back(static_cast<unsigned char>(n >> 16));
  f.push_back(static_cast<unsigned char>(n >> 8));
  f.push_back(static_cast<unsigned char>(n));
  f.push_back(0);
  f.push_back(0);
  f.insert(f.end(), payload.begin(), payload.end());
  return f;
}

std::vector<unsigned char> id3v23(const std::vector<unsigned char>& frames) {
  std::vector<unsigned char> h = {'I', 'D', '3', 3, 0, 0};
  const std::uint32_t n = static_cast<std::uint32_t>(frames.size());
  h.push_back((n >> 21) & 0x7F);
  h.push_back((n >> 14) & 0x7F);
  h.push_back((n >> 7) & 0x7F);
  h.push_back(n & 0x7F);
  h.insert(h.end(), frames.begin(), frames.end());
  return h;
}

}  // namespace

TEST(tags_id3v1) {
  std::vector<unsigned char> b(200, 0x55);
  for (unsigned char c : std::string("TAG")) b.push_back(c);
  pad(b, "Old Title", 30);
  pad(b, "Old Artist", 30);
  pad(b, "Old Album", 30);
  pad(b, "1999", 4);
  pad(b, "", 30);
  b.push_back(17);
  const auto p = testing::scratch() / "v1.mp3";
  writeFile(p, b);
  Tags t;
  CHECK(readTags(p.string(), t));
  CHECK_EQ(t.title, std::string("Old Title"));
  CHECK_EQ(t.artist, std::string("Old Artist"));
  CHECK_EQ(t.album, std::string("Old Album"));
}

TEST(tags_id3v2_latin1_and_utf16) {
  std::vector<unsigned char> title = {0, 'H', 'e', 'l', 'l', 'o'};
  // UTF-16LE with BOM: "Björk"
  std::vector<unsigned char> artist = {1, 0xFF, 0xFE, 'B', 0, 'j', 0, 0xF6, 0, 'r', 0, 'k', 0};
  std::vector<unsigned char> frames = frame("TIT2", title);
  const auto f2 = frame("TPE1", artist);
  frames.insert(frames.end(), f2.begin(), f2.end());
  std::vector<unsigned char> file = id3v23(frames);
  file.resize(file.size() + 64, 0);  // padding / "audio"
  const auto p = testing::scratch() / "v2.mp3";
  writeFile(p, file);
  Tags t;
  CHECK(readTags(p.string(), t));
  CHECK_EQ(t.title, std::string("Hello"));
  CHECK_EQ(t.artist, std::string("Bj\xC3\xB6rk"));  // UTF-8 for o-umlaut
}

TEST(tags_decode_utf8_frames) {
  const unsigned char d[] = {3, 'C', 'a', 'f', 0xC3, 0xA9};
  CHECK_EQ(decodeId3Text(d, sizeof d), std::string("Caf\xC3\xA9"));
  const unsigned char unknown[] = {9, 'x'};
  CHECK_EQ(decodeId3Text(unknown, sizeof unknown), std::string());
}

TEST(tags_missing_returns_false) {
  const auto p = testing::scratch() / "none.mp3";
  writeFile(p, std::vector<unsigned char>(300, 0x11));
  Tags t;
  CHECK(!readTags(p.string(), t));
  CHECK(!readTags((testing::scratch() / "does-not-exist.mp3").string(), t));
}

TEST(tags_feed_playlist_titles) {
  std::vector<unsigned char> frames = frame("TIT2", {0, 'S', 'o', 'n', 'g'});
  const auto f2 = frame("TPE1", {0, 'B', 'a', 'n', 'd'});
  frames.insert(frames.end(), f2.begin(), f2.end());
  auto file = id3v23(frames);
  file.resize(file.size() + 16, 0);
  const auto p = testing::scratch() / "tagged-title.mp3";
  writeFile(p, file);
  Playlist pl;
  pl.addPath(p.string());
  CHECK_EQ(pl.at(0).title, std::string("Band - Song"));

  const auto plain = testing::scratch() / "just a filename.mp3";
  writeFile(plain, std::vector<unsigned char>(64, 0));
  pl.addPath(plain.string());
  CHECK_EQ(pl.at(1).title, std::string("just a filename"));
}
