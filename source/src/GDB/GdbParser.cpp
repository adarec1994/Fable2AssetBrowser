#include "GdbParser.h"
#include "GdbReaderInternal.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <memory>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Gdb {

namespace {
#include "GdbParser/Core/Definitions.inl"
#include "GdbParser/Transforms/RecordTransforms.inl"
#include "GdbParser/Models/FieldReaders.inl"
#include "GdbParser/Models/ModelResolution.inl"
#include "GdbParser/Core/Names.inl"
#include "GdbParser/Transforms/InlineTransforms.inl"

}

#include "GdbParser/Records/Parsing.inl"
#include "GdbParser/Records/ModelLookup.inl"
#include "GdbParser/Records/Rows.inl"

namespace {

#include "GdbParser/Particles/NameLookup.inl"

}

#include "GdbParser/Particles/Effects.inl"
#include "GdbParser/Particles/Attachments.inl"
#include "GdbParser/Particles/Placements.inl"

namespace {

#include "GdbParser/Entities/MultiGdb.inl"

}

#include "GdbParser/Entities/Contents.inl"
#include "GdbParser/Entities/Gameplay.inl"
#include "GdbParser/Entities/Properties.inl"
#include "GdbParser/Entities/GameplayOptions.inl"
#include "GdbParser/Core/EmbeddedDictionary.inl"
#include "GdbParser/Entities/PropTemplates.inl"

namespace {

#include "GdbParser/Entities/ModelResolution.inl"

}

#include "GdbParser/Entities/CreatureCatalog.inl"
#include "GdbParser/Entities/TextTags.inl"
#include "GdbParser/Entities/Spawns.inl"
#include "GdbParser/Entities/Transitions.inl"
#include "GdbParser/Entities/AnimTree.inl"

namespace {

#include "GdbParser/Items/LabelFormatting.inl"

}

#include "GdbParser/Items/Catalog.inl"
#include "GdbParser/Items/Details.inl"

}
