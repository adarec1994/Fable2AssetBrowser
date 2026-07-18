#include "Lua.h"
#include "Utilities/Progress.h"
#include "Utilities/State.h"
#include "Utilities/Files.h"
#include "lua/lua_decompile.h"
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <cstring>
#include <thread>
#include <map>
#include <stack>

namespace lua51 {

#include "lua/Bytecode/Definitions.inl"
#include "lua/Bytecode/Reader.inl"

class Decompiler {
#include "lua/Decompiler/StateAndHelpers.inl"
#include "lua/Decompiler/Driver.inl"
#include "lua/Decompiler/Instructions/Values.inl"
#include "lua/Decompiler/Instructions/Flow.inl"
#include "lua/Decompiler/Instructions/Calls.inl"
#include "lua/Decompiler/Instructions/LoopsAndClosures.inl"
};

#include "lua/API/Decompile.inl"

}

#include "lua/API/Bridge.inl"
#include "lua/Paths/Output.inl"
#include "lua/Dump/Read.inl"
#include "lua/Dump/Single.inl"
#include "lua/Dump/All.inl"
