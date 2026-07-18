#include "AssetImport.h"

#include "GlbImport.h"
#include "ImageLoad.h"
#include "MdlWriter.h"

#include "Entity/StaticPropAuthoring.h"
#include "MDL/ModelParser.h"
#include "textures/TexParser.h"
#include "UI/OutputLog.h"
#include "Utilities/DebugTrace.h"
#include "Utilities/Progress.h"
#include "Utilities/GameBackup.h"
#include "Utilities/State.h"
#include "Utilities/Utils.h"

#include "BNKCore.cpp"
#include "Level/IO/BnkWriter.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>


extern void tree_register_injected_file(const std::string& bnk_path,
                                        const std::string& virtual_path);

namespace AssetImport {
namespace {

#include "AssetImport/Bank/InjectionAndValidation.inl"
#include "AssetImport/Textures/Targets.inl"
#include "AssetImport/Textures/Resolve.inl"
#include "AssetImport/Textures/Replace.inl"
#include "AssetImport/Models/SpawnTemplate.inl"

}

#include "AssetImport/Naming/Sanitize.inl"
#include "AssetImport/Models/ImportGlb.inl"
#include "AssetImport/Textures/ImportImage.inl"
#include "AssetImport/Textures/ReplaceSelected.inl"
#include "AssetImport/Folders/Import.inl"

}
