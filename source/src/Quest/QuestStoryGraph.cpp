#include "QuestStoryGraph.h"

#include <algorithm>
#include <cctype>
#include <deque>
#include <iomanip>
#include <iterator>
#include <limits>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace Quest {
namespace {

#include "QuestStoryGraph/Core/TypesAndText.inl"
#include "QuestStoryGraph/Parsing/Classification.inl"
#include "QuestStoryGraph/Story/RewardsAndKinds.inl"
#include "QuestStoryGraph/Story/BeatConstruction.inl"
#include "QuestStoryGraph/Layout/Branching.inl"
#include "QuestStoryGraph/Story/ChildhoodHelpers.inl"
#include "QuestStoryGraph/Story/NodeEmission.inl"
#include "QuestStoryGraph/Timelines/Childhood.inl"
#include "QuestStoryGraph/References/Metadata.inl"
#include "QuestStoryGraph/Timelines/Frankenbride.inl"

}

#include "QuestStoryGraph/Public/BuildStoryGraph.inl"

}
