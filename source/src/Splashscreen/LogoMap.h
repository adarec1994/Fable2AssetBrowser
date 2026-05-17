#pragma once

#include <vector>
#include <utility>

namespace Splash {

extern const char* const kLogoMap[27];
constexpr int kLogoRows = 27;
constexpr int kLogoCols = 200;

const std::vector<std::pair<float, float>>& get_letter_positions();

}
