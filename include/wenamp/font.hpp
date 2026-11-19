#pragma once
#include <cstdint>

namespace wenamp {

// Five column bytes per glyph (bit 0 = top row) for printable ASCII 0x20..0x7E.
// Anything else renders as '?'.
const std::uint8_t* glyph5x7(unsigned char ch);

}  // namespace wenamp
