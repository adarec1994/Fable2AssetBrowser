#include "LevelEdit.h"

#include "../../Utilities/GameBackup.h"
#include "../Creation/FoliageAuthoring.h"
#include "../Creation/LandscapeAuthoring.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <zlib.h>

#include "BNKCore.cpp"
#include "ISO/IsoMount.h"
#include "ISO/IsoWriteback.h"
#include "UI/OutputLog.h"
#include "Utilities/Progress.h"
#include "Utilities/DebugLog.h"
#include "Utilities/State.h"
#include "Level/IO/BnkWriter.h"
#include "GDB/GdbEdit.h"
#include "Level/Core/LevelLoader.h"
#include "Level/Database/TextBank.h"

namespace LevelEdit {
namespace {
#include "LevelEdit/Internal/State.inl"
#include "LevelEdit/Internal/Compression.inl"
#include "LevelEdit/Serialization/LevelPlacements.inl"
#include "LevelEdit/Assets/DependencyLookup.inl"
#include "LevelEdit/Persistence/Sidecars.inl"
#include "LevelEdit/Internal/Transforms.inl"

}

#include "LevelEdit/Editing/Lifecycle.inl"
#include "LevelEdit/Editing/Containers.inl"
#include "LevelEdit/Editing/Spawns.inl"
#include "LevelEdit/Editing/Transforms.inl"

namespace {

#include "LevelEdit/Saving/ContainerEdits.inl"
#include "LevelEdit/Saving/EntityCreation.inl"
#include "LevelEdit/Saving/SpawnPoints.inl"
#include "LevelEdit/Saving/Generators.inl"
#include "LevelEdit/Saving/EntityRemoval.inl"
#include "LevelEdit/Saving/SaveRouting.inl"

}

#include "LevelEdit/Saving/Save.inl"
#include "LevelEdit/Saving/WorkingCopy.inl"

}
