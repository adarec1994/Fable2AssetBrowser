#include "AnimBank.h"
#include "AnimDataFile.h"

#include "../UI/OutputLog.h"
#include "../ISO/IsoMount.h"
#include "../Utilities/State.h"
#include "../BNKCore.cpp"

#include <zlib.h>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

std::string read_lua_file_content(const std::string& path);

namespace Anim {

namespace {

#include "AnimBank/State/Reader.inl"

}

#include "AnimBank/Toc/Parse.inl"
#include "AnimBank/Toc/Load.inl"
#include "AnimBank/Clips/Duration.inl"

std::string read_lua_file_content(const std::string& path);

namespace {

#include "AnimBank/Gdb/BasicHelpers.inl"
#include "AnimBank/Gdb/View.inl"
#include "AnimBank/Gdb/StatsAndNames.inl"
#include "AnimBank/Gdb/Traversal.inl"
#include "AnimBank/Gdb/ClipRefs.inl"
#include "AnimBank/Gdb/Bindings.inl"
#include "AnimBank/Gdb/Scanning.inl"

}

#include "AnimBank/Resolve/Lua.inl"
#include "AnimBank/Resolve/Gdb.inl"

namespace {

#include "AnimBank/Cache/Internals.inl"

}

#include "AnimBank/Cache/Load.inl"
#include "AnimBank/Cache/Save.inl"
#include "AnimBank/Cache/Baked.inl"
#include "AnimBank/Bindings/Public.inl"

}
