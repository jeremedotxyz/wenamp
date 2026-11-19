#pragma once
#include <string>

namespace wenamp {

struct Tags {
  std::string title;
  std::string artist;
  std::string album;
  bool empty() const { return title.empty() && artist.empty() && album.empty(); }
};

// Reads ID3v2.3/2.4 (TIT2, TPE1, TALB) and falls back to ID3v1. Returns true
// when at least one field was found. Text is returned as UTF-8.
bool readTags(const std::string& path, Tags& out);

// Exposed for tests: decode an ID3v2 text-frame payload (encoding byte first).
std::string decodeId3Text(const unsigned char* data, std::size_t size);

}  // namespace wenamp
