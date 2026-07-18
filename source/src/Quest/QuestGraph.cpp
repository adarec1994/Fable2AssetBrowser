#include "QuestGraph.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace Quest {
namespace {

#include "QuestGraph/Core/TypesAndText.inl"
#include "QuestGraph/Parsing/ValuesAndWorld.inl"
#include "QuestGraph/Parsing/FactsAndReferences.inl"
#include "QuestGraph/Narrative/Conditions.inl"
#include "QuestGraph/Narrative/DescribeStart.inl"
#include "QuestGraph/Narrative/DescribeWorld.inl"
#include "QuestGraph/Narrative/DescribeActions.inl"
#include "QuestGraph/Narrative/Fallback.inl"
#include "QuestGraph/Narrative/Collection.inl"
#include "QuestGraph/Graph/ConstructionHelpers.inl"

}

#include "QuestGraph/Public/StringLiterals.inl"
#include "QuestGraph/Public/Names.inl"
#include "QuestGraph/Public/BuildGraph.inl"

}
