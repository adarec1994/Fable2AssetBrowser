#include "XenosShaderBinary.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace XenosShaderBinary {
namespace {

#include "XenosShaderBinary/Metadata/Tables.inl"
#include "XenosShaderBinary/Formatting/Destinations.inl"
#include "XenosShaderBinary/Formatting/Sources.inl"
#include "XenosShaderBinary/Formatting/Instructions.inl"
#include "XenosShaderBinary/Decode/ControlFlowBits.inl"
#include "XenosShaderBinary/Decode/AluCore.inl"

}

#include "XenosShaderBinary/Decode/VertexFetch.inl"
#include "XenosShaderBinary/Patching/VertexFetch.inl"
#include "XenosShaderBinary/Decode/Alu.inl"
#include "XenosShaderBinary/Decode/TextureFetch.inl"
#include "XenosShaderBinary/Decode/Program.inl"
#include "XenosShaderBinary/Metadata/OpcodeNames.inl"

}
