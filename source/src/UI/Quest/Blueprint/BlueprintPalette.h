#pragma once

#include "Quest/Blueprint/BlueprintGraph.h"

#include "imgui.h"

namespace BlueprintUIDetail {






int DrawPaletteContents(Quest::Bp::BlueprintQuest& quest, ImVec2 spawn_pos,
                        int pending_pin);

}
