#include "../UI_Panels.h"
#include "PanelInternal.h"
#include "NewLevelDialog.h"
#include "../UI_Main.h"
#include "../OutputLog.h"
#include "../ModelPreview.h"
#include "../EntityModelResolver.h"
#include "../ContentTabs.h"
#include "DetailsPanel.h"
#include "../../Level/Creation/GameRegistry.h"
#include "../../Level/Creation/LandscapeAuthoring.h"
#include "../../Level/Creation/NewLevel.h"
#include "../Quest/QuestNodeView.h"
#include "../AnimTree/AnimTreeView.h"

#include "../../ISO/IsoDump.h"
#include "../../Quest/QuestInjection.h"
#include "../../Entity/NpcAuthoring.h"
#include "../../Entity/StaticPropAuthoring.h"
#include "../../Level/Editing/LevelEdit.h"
#include "../../Level/Core/LevelLoader.h"
#include "../../Level/Database/TextBank.h"
#include "../../Level/Core/LevelExport.h"
#include "../../animations/AnimDataFile.h"
#include "../../animations/AnimPlayer.h"
#include "../../animations/AnimRigMap.h"
#include "../../MDL/ModelParser.h"

#include "../../Lua.h"
#include "../../Utilities/Progress.h"
#include "../../Utilities/Utils.h"
#include "../../Utilities/GameBackup.h"
#include "../../BNKCore.cpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_stdlib.h"
#include "IconsFontAwesome6.h"
#include <filesystem>
#include <algorithm>
#include <atomic>
#include <limits>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <cstring>
#include <cmath>

extern ModelPreview g_mp;

namespace {

#include "LeftPanel/State/EntityCreation.inl"

}

#include "LeftPanel/Layout/Dimensions.inl"
#include "LeftPanel/Assets/Selection.inl"

namespace {

#include "LeftPanel/Drill/State.inl"
#include "LeftPanel/Scripts/Selection.inl"
#include "LeftPanel/Quests/Selection.inl"
#include "LeftPanel/Quests/Injection.inl"
#include "LeftPanel/Drill/Navigation.inl"
#include "LeftPanel/EntityAuthoring/Fields.inl"
#include "LeftPanel/EntityAuthoring/Pickers.inl"
#include "LeftPanel/EntityAuthoring/Modal.inl"

}

#include "LeftPanel/Quests/Search.inl"

#include "LeftPanel/Drill/TreeOpening.inl"

#ifdef _WIN32
void draw_left_panel(ID3D11Device* device) {
#else
void draw_left_panel() {
#endif

#include "LeftPanel/Draw/Layout/Tabs.inl"
#include "LeftPanel/Draw/Assets/FlatAssetList.inl"
#include "LeftPanel/Draw/Tabs/Banks.inl"
#include "LeftPanel/Draw/Tabs/FileTree.inl"
#include "LeftPanel/Draw/Tabs/AssetLists.inl"
#include "LeftPanel/Draw/Tabs/Animations.inl"
#include "LeftPanel/Draw/Tabs/Items.inl"
#include "LeftPanel/Draw/Tabs/Entities.inl"
#include "LeftPanel/Draw/Tabs/Quests.inl"
#include "LeftPanel/Draw/Tabs/Levels.inl"
#include "LeftPanel/Draw/Tabs/ScriptsAndDetails.inl"
    ImGui::EndChild();
}

void RequestQuestInjection() { inject_active_authored_quest(); }
bool QuestInjectionBusy() { return g_quest_injection_busy.load(); }
