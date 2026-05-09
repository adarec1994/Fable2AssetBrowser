// ASCII-art representation of the Fable 2 logo, used by the splash screen
// to seed sparkle particles along the visible glyphs. Defined separately
// from the splash logic to keep the noisy 27-line string array out of the
// way.

#pragma once

#include <vector>
#include <utility>

namespace Splash {

// 27 lines, each up to 200 chars wide. Non-space characters mark "glyph
// pixels"; spaces are background.
extern const char* const kLogoMap[27];
constexpr int kLogoRows = 27;
constexpr int kLogoCols = 200;

// Returns a list of normalized [0,1] x [0,1] (x, y) positions for every
// non-space cell in the logo map. Built once and cached.
const std::vector<std::pair<float, float>>& get_letter_positions();

} // namespace Splash
