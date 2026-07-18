#include "ModelParser.h"
#include "../Utilities/Files.h"
#include "../Utilities/Utils.h"
#include "../Utilities/State.h"
#include "../BNKCore.cpp"
#include <algorithm>
#include <unordered_map>
#include <filesystem>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <fstream>
#include <cstdarg>
#include <cstdio>
#include <optional>

using std::uint8_t; using std::uint16_t; using std::uint32_t;

#include "ModelParser/Loading/BufferAssembly.inl"

namespace {
#include "ModelParser/Core/BinaryUtilities.inl"
}

#include "ModelParser/Parsing/Info/HeaderAndMaterials.inl"
#include "ModelParser/Parsing/Info/Foliage.inl"
#include "ModelParser/Parsing/Info/StandardBuffers.inl"
#include "ModelParser/Cloth/Blocks.inl"
#include "ModelParser/Cloth/Geometry.inl"

namespace {

#include "ModelParser/Engine/Records.inl"

}

#include "ModelParser/Engine/Geometry.inl"
#include "ModelParser/Geometry/Main.inl"
#include "ModelParser/Reparse/Polymsh.inl"
#include "ModelParser/Reparse/MissingBuffers.inl"
#include "ModelParser/Reparse/Foliage.inl"
#include "ModelParser/Reparse/MultiInstance.inl"
