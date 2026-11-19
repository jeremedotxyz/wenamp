#include "wenamp/tags.hpp"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

namespace wenamp {

namespace {

void appendUtf8(std::string& s, std::uint32_t cp) {
  if (cp < 0x80) {
    s += static_cast<char>(cp);
  } else if (cp < 0x800) {
    s += static_cast<char>(0xC0 | (cp >> 6));
    s += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    s += static_cast<char>(0xE0 | (cp >> 12));
    s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    s += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    s += static_cast<char>(0xF0 | (cp >> 18));
    s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    s += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

std::string trim(std::string s) {
  while (!s.empty() && (s.back() == '\0' || s.back() == ' ' || s.back() == '\r' || s.back() == '\n'))
    s.pop_back();
  std::size_t i = 0;
  while (i < s.size() && s[i] == ' ') ++i;
  return s.substr(i);
}

std::string decodeUtf16(const unsigned char* p, std::size_t n, bool bigEndian) {
  std::string out;
  for (std::size_t i = 0; i + 1 < n; i += 2) {
    std::uint32_t u = bigEndian ? (p[i] << 8 | p[i + 1]) : (p[i + 1] << 8 | p[i]);
    if (u == 0) break;
    if (u >= 0xD800 && u <= 0xDBFF && i + 3 < n) {
      std::uint32_t lo = bigEndian ? (p[i + 2] << 8 | p[i + 3]) : (p[i + 3] << 8 | p[i + 2]);
      if (lo >= 0xDC00 && lo <= 0xDFFF) {
        u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
        i += 2;
      }
    }
    appendUtf8(out, u);
  }
  return out;
}

std::uint32_t synchsafe(const unsigned char* p) {
  return (std::uint32_t(p[0] & 0x7F) << 21) | (std::uint32_t(p[1] & 0x7F) << 14) |
         (std::uint32_t(p[2] & 0x7F) << 7) | std::uint32_t(p[3] & 0x7F);
}

std::uint32_t bigEndian32(const unsigned char* p) {
  return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) |
         std::uint32_t(p[3]);
}

bool readId3v2(std::ifstream& f, Tags& t) {
  unsigned char h[10];
  f.seekg(0);
  if (!f.read(reinterpret_cast<char*>(h), 10)) return false;
  if (std::memcmp(h, "ID3", 3) != 0) return false;
  const int major = h[3];
  if (major != 3 && major != 4) return false;
  const std::uint32_t size = synchsafe(h + 6);
  if (size == 0 || size > 16u * 1024 * 1024) return false;
  std::vector<unsigned char> body(size);
  if (!f.read(reinterpret_cast<char*>(body.data()), size)) return false;

  std::size_t pos = 0;
  if (h[5] & 0x40) {  // extended header
    if (body.size() < 4) return false;
    const std::uint32_t ext = major == 4 ? synchsafe(body.data()) : bigEndian32(body.data()) + 4;
    pos = ext;
  }
  while (pos + 10 <= body.size()) {
    const unsigned char* fr = &body[pos];
    if (fr[0] == 0) break;  // padding
    const std::string id(reinterpret_cast<const char*>(fr), 4);
    const std::uint32_t fsz = major == 4 ? synchsafe(fr + 4) : bigEndian32(fr + 4);
    pos += 10;
    if (fsz == 0 || pos + fsz > body.size()) break;
    const std::string text = decodeId3Text(&body[pos], fsz);
    if (id == "TIT2") t.title = text;
    else if (id == "TPE1") t.artist = text;
    else if (id == "TALB") t.album = text;
    pos += fsz;
  }
  return !t.empty();
}

std::string latin1ToUtf8(const unsigned char* p, std::size_t n) {
  std::string out;
  for (std::size_t i = 0; i < n; ++i) {
    if (p[i] == 0) break;
    appendUtf8(out, p[i]);
  }
  return out;
}

bool readId3v1(std::ifstream& f, Tags& t) {
  f.seekg(0, std::ios::end);
  const std::streamoff len = f.tellg();
  if (len < 128) return false;
  unsigned char b[128];
  f.seekg(-128, std::ios::end);
  if (!f.read(reinterpret_cast<char*>(b), 128)) return false;
  if (std::memcmp(b, "TAG", 3) != 0) return false;
  t.title = trim(latin1ToUtf8(b + 3, 30));
  t.artist = trim(latin1ToUtf8(b + 33, 30));
  t.album = trim(latin1ToUtf8(b + 63, 30));
  return !t.empty();
}

}  // namespace

std::string decodeId3Text(const unsigned char* data, std::size_t size) {
  if (size < 1) return {};
  const int enc = data[0];
  const unsigned char* p = data + 1;
  const std::size_t n = size - 1;
  switch (enc) {
    case 0: return trim(latin1ToUtf8(p, n));
    case 1: {
      if (n >= 2 && p[0] == 0xFF && p[1] == 0xFE) return trim(decodeUtf16(p + 2, n - 2, false));
      if (n >= 2 && p[0] == 0xFE && p[1] == 0xFF) return trim(decodeUtf16(p + 2, n - 2, true));
      return trim(decodeUtf16(p, n, false));
    }
    case 2: return trim(decodeUtf16(p, n, true));
    case 3: {
      std::string s(reinterpret_cast<const char*>(p), n);
      const auto z = s.find('\0');
      if (z != std::string::npos) s.resize(z);
      return trim(s);
    }
    default: return {};
  }
}

bool readTags(const std::string& path, Tags& out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  Tags t;
  if (readId3v2(f, t)) {
    out = t;
    return true;
  }
  f.clear();
  Tags v1;
  if (readId3v1(f, v1)) {
    out = v1;
    return true;
  }
  return false;
}

}  // namespace wenamp
