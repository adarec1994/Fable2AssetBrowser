#include "QuestNodeView.h"

#include "Blueprint/BlueprintEditor.h"
#include "../../Quest/QuestAuthoring.h"
#include "../../Quest/QuestGraph.h"
#include "../../Quest/QuestReferences.h"
#include "../../Quest/QuestRewards.h"
#include "../../Quest/QuestStoryGraph.h"
#include "../../Quest/QuestWorldIndex.h"
#include "../../Level/Core/LevelLoader.h"
#include "../../Level/Editing/LevelEdit.h"
#include "../../Level/Database/TextBank.h"
#include "../../Utilities/State.h"
#include "../../animations/AnimBank.h"

#include "imgui.h"
#include "imgui_stdlib.h"
#include "imgui_node_editor.h"
#include "IconsFontAwesome6.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace QuestUI {
namespace {

#include "QuestNodeView/State/StateAndCatalog.inl"
#include "QuestNodeView/Rendering/IdsColorsAndEditor.inl"
#include "QuestNodeView/Rendering/Pins.inl"
#include "QuestNodeView/Rendering/GraphNode.inl"
#include "QuestNodeView/Authoring/StoryMilestones.inl"
#include "QuestNodeView/Authoring/NodePresentation.inl"
#include "QuestNodeView/Pickers/Items.inl"
#include "QuestNodeView/References/Summary.inl"

}

#include "QuestNodeView/Authoring/QuestLifecycle.inl"
#include "QuestNodeView/Authoring/Prerequisites/Requests.inl"
#include "QuestNodeView/References/Binding.inl"
#include "QuestNodeView/Authoring/Prerequisites/Conditions.inl"
#include "QuestNodeView/Authoring/Npc/Creation.inl"
#include "QuestNodeView/Authoring/Npc/Instances.inl"
#include "QuestNodeView/ReadOnly/Helpers.inl"
#include "QuestNodeView/ReadOnly/Inspector.inl"
#include "QuestNodeView/References/Catalog.inl"
#include "QuestNodeView/Graph/Source.inl"
#include "QuestNodeView/Graph/ViewState.inl"
#include "QuestNodeView/Graph/Draw.inl"
#include "QuestNodeView/Graph/Shutdown.inl"

}
