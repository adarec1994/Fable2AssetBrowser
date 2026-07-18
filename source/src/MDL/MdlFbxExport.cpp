#include "MdlFbxExport.h"
#include "ModelParser.h"
#include "MdlExportCommon.h"
#include "MdlTexExport.h"
#include "../textures/TexParser.h"
#include "../Utilities/State.h"
#include "../Utilities/Utils.h"
#include "../Utilities/Files.h"
#include "../UI/OutputLog.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <vector>
#include <string>
#include <chrono>

extern bool mdl_export_decode_texture_to_png(
    const std::vector<unsigned char>& tex_buf,
    std::vector<uint8_t>& png_out);

extern bool build_any_tex_buffer_for_name(const std::string& tex_name,
                                          std::vector<unsigned char>& out,
                                          const std::string& preferred_bnk);

namespace {

#include "MdlFbxExport/Encoding/BufferAndProperties.inl"
#include "MdlFbxExport/Encoding/Nodes.inl"
#include "MdlFbxExport/Math/Types.inl"
#include "MdlFbxExport/Math/Transforms.inl"

}

#include "MdlFbxExport/Export/Setup.inl"
#include "MdlFbxExport/Export/Materials.inl"
#include "MdlFbxExport/Export/Animations.inl"
#include "MdlFbxExport/Export/Document.inl"
#include "MdlFbxExport/Export/Objects/Bones.inl"
#include "MdlFbxExport/Export/Objects/Meshes.inl"
#include "MdlFbxExport/Export/Objects/Animation.inl"
#include "MdlFbxExport/Export/Connections.inl"
#include "MdlFbxExport/Export/Write.inl"
