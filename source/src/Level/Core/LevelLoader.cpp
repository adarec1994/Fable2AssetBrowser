#include "LevelLoader.h"
#include "Level/Terrain/HeightfieldLoader.h"
#include "Level/Creation/LandscapeAuthoring.h"
#include "Level/Editing/LevelEdit.h"
#include "Level/Terrain/TextureAtlasDecoder.h"
#include "Level/Terrain/EhfPalette.h"
#include "Level/Terrain/EhfChunkParser.h"
#include "Level/Terrain/TerrainTextureRegistry.h"
#include "Level/Loading/LevelBinaryReader.h"
#include "Level/Loading/LevelCatalogLoaderInternal.h"
#include "Level/Loading/LevelTerrainLoaderInternal.h"
#include "Level/IO/VfsConfig.h"
#include "GDB/GdbModelHashlist.h"
#include "GDB/GdbParser.h"
#include "Level/Database/TextBank.h"
#include "Level/Effects/ParticleBank.h"
#include "Level/Effects/ParticleFX.h"
#include "MDL/ModelParser.h"
#include "Havok/HavokPackfileReader.h"
#include "ISO/IsoMount.h"

#include "Utilities/State.h"
#include "Utilities/Utils.h"
#include "Utilities/Progress.h"
#include "BNKCore.cpp"
#include "UI/OutputLog.h"
#include "textures/TexParser.h"
#include "textures/LhTexCodec.h"
#include "textures/export/TextureExport.h"
#include <zlib.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <vector>
#include <cstdint>
extern bool decode_tex_to_rgba(const std::vector<unsigned char>& blob,
                               std::vector<uint8_t>& rgba,
                               int& out_w, int& out_h,
                               bool* out_has_alpha,
                               int mip_index = -1);
extern const std::string& mp_last_decode_fail_reason();
extern const std::string& mp_last_decode_info();

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <thread>
#include <iomanip>
#include <iterator>
#include <climits>
#include <limits>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "LevelLoader/State/Globals.inl"
#include "LevelLoader/Platform/LinuxPreview.inl"
#include "LevelLoader/Models/GlobalLookup.inl"

namespace Level {

#include "LevelLoader/Async/Loading.inl"

namespace {

#include "Level/Loading/Stages/LevelPropSupport.inl"

}

#include "LevelLoader/Open/Progress.inl"
#include "LevelLoader/Open/Parsing.inl"
#include "LevelLoader/Open/Resources/Heightfields.inl"
#include "LevelLoader/Open/Resources/Vfs.inl"
#include "LevelLoader/Open/Content.inl"
#include "LevelLoader/Open/Collision.inl"
#include "LevelLoader/Open/Terrain/Main.inl"
#include "LevelLoader/Open/Terrain/Adjacent/Helpers.inl"
#include "LevelLoader/Open/Terrain/Adjacent/Loading.inl"
#include "LevelLoader/Open/Terrain/PendingState.inl"
#include "LevelLoader/Open/Terrain/Props.inl"
#include "LevelLoader/Open/Terrain/PlacementValidation.inl"
#include "LevelLoader/Open/Terrain/Water.inl"
#include "LevelLoader/Open/Terrain/Handoff.inl"

}
