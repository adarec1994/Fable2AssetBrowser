#include "QuestAuthoring.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <iterator>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace Quest {
namespace {

#include "QuestAuthoring/Lua/Helpers.inl"
#include "QuestAuthoring/Flow/Validation.inl"
#include "QuestAuthoring/Childhood/Validation.inl"
#include "QuestAuthoring/Childhood/Generate.inl"

}

#include "QuestAuthoring/Model/Quest.inl"
#include "QuestAuthoring/Metadata/Names.inl"
#include "QuestAuthoring/Metadata/Milestones.inl"
#include "QuestAuthoring/Validation/Simple.inl"
#include "QuestAuthoring/Text/Entries.inl"
#include "QuestAuthoring/Generation/QuestLua.inl"
#include "QuestAuthoring/Generation/Eligibility.inl"
#include "QuestAuthoring/Generation/Preview.inl"
#include "QuestAuthoring/Patching/Gameflow.inl"

}
