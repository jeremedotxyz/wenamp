#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace wenamp {

// Every colour the procedural skin uses. There are no bitmap assets: the whole
// UI is drawn from rectangles, a 5x7 font and seven-segment digits.
struct Theme {
  std::string name;
  std::uint32_t body;       // window face
  std::uint32_t bodyHi;     // bevel highlight
  std::uint32_t bodyLo;     // bevel shadow
  std::uint32_t titleA;     // title bar gradient start
  std::uint32_t titleB;     // title bar gradient end
  std::uint32_t titleText;
  std::uint32_t well;       // recessed LCD background
  std::uint32_t lcdOn;      // active LCD / text
  std::uint32_t lcdDim;     // inactive LCD segments / labels
  std::uint32_t accent;     // toggles that are on, current playlist row
  std::uint32_t buttonFace;
  std::uint32_t buttonIcon;
  std::uint32_t sliderTrack;
  std::uint32_t sliderThumb;
  std::uint32_t barLow;     // visualizer gradient bottom
  std::uint32_t barMid;
  std::uint32_t barHigh;
  std::uint32_t peak;
  std::uint32_t listBg;
  std::uint32_t listText;
  std::uint32_t listSel;    // selected row background

  static Theme classic();  // steel + green LCD
  static Theme ember();    // warm dark with an ember accent
  static Theme paper();    // light editorial
  static Theme byName(const std::string& name);  // falls back to classic
  static std::vector<std::string> names();
};

}  // namespace wenamp
