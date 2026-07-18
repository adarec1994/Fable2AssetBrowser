#include "EnvironmentThemeParser.h"
#include "GDB/GdbReaderInternal.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <vector>

namespace Gdb {

namespace {

#include "EnvironmentThemeParser/Schema/ConstantsAndTypes.inl"

class EnvironmentThemeExtractor {
public:

#include "EnvironmentThemeParser/Extractor/Public.inl"
#include "EnvironmentThemeParser/Extractor/Lookup.inl"
#include "EnvironmentThemeParser/Extractor/Apply/Water.inl"
#include "EnvironmentThemeParser/Extractor/Apply/Sky.inl"
#include "EnvironmentThemeParser/Extractor/Apply/Weather.inl"
#include "EnvironmentThemeParser/Extractor/Apply/Cloud.inl"
#include "EnvironmentThemeParser/Extractor/Selection.inl"

};

}

#include "EnvironmentThemeParser/PublicApi.inl"

}
