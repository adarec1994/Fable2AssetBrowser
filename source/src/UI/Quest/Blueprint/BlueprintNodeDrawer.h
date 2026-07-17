#pragma once

#include "Quest/Blueprint/BlueprintGraph.h"

#include "imgui.h"
#include <imgui_node_editor.h>

namespace BlueprintUIDetail {





void DrawNode(Quest::Bp::BlueprintQuest& quest, Quest::Bp::Node& node,
              int severity = 0);

ImU32 PinColorU32(Quest::Bp::PinType type);

}
