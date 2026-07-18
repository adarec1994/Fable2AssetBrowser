#include "Level/Core/LevelLoader.h"
#include "Level/Terrain/HeightfieldLoader.h"
#include "Level/Terrain/TextureAtlasDecoder.h"
#include "Level/Terrain/EhfPalette.h"
#include "Level/Terrain/EhfChunkParser.h"
#include "Level/Terrain/TerrainTextureRegistry.h"
#include "Level/Loading/LevelBinaryReader.h"
#include "Level/Loading/LevelTerrainLoaderInternal.h"
#include "BNKCore.cpp"
#include "UI/OutputLog.h"
#include "Utilities/State.h"
#include "textures/TexParser.h"
#include "textures/LhTexCodec.h"
#include "textures/export/TextureExport.h"
#include <zlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

extern bool decode_tex_to_rgba(const std::vector<unsigned char>& blob,
                               std::vector<uint8_t>& rgba,
                               int& out_w, int& out_h,
                               bool* out_has_alpha,
                               int mip_index);
extern const std::string& mp_last_decode_fail_reason();
extern const std::string& mp_last_decode_info();

namespace Level {

#include "LevelTerrainLoader/Preview/Heightmap.inl"
#include "LevelTerrainLoader/Atlas/Decode.inl"

namespace {

#include "LevelTerrainLoader/Tiles/DescriptorsAndParsing.inl"
#include "LevelTerrainLoader/Tiles/EmbeddedBc1.inl"
#include "LevelTerrainLoader/Tiles/Compositing.inl"
#include "LevelTerrainLoader/Vista/PageDecode.inl"
#include "LevelTerrainLoader/Vista/PatchGeometry.inl"

}

#include "LevelTerrainLoader/Vista/Public.inl"
#include "LevelTerrainLoader/Albedo/Decode.inl"
#include "LevelTerrainLoader/Albedo/PaletteFallback.inl"
#include "LevelTerrainLoader/Composite/Wrappers.inl"
#include "LevelTerrainLoader/Composite/Setup.inl"
#include "LevelTerrainLoader/Composite/Sampling.inl"
#include "LevelTerrainLoader/Composite/Paint.inl"

}
