#include "../UI_Panels.h"
#include "PanelInternal.h"
#include "../UI_Main.h"
#include "../HexView.h"
#include "../ModelPreview.h"
#include "../ContentTabs.h"
#include "../../textures/TexParser.h"
#include "../../textures/LhTexCodec.h"
#include "../../MDL/ModelParser.h"
#include "../../MDL/mdl_converter.h"
#include "../../animations/AnimBank.h"
#include "../../Utilities/Utils.h"
#include "../../Utilities/Files.h"
#include "../../Utilities/operations.h"
#include "../../Utilities/Progress.h"
#include "../../BNKCore.cpp"
#include "../../Lua.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_stdlib.h"
#include <filesystem>
#include <algorithm>
#include <atomic>
#include <thread>
#include <ctime>
#include <cstring>

#ifdef _WIN32
void draw_right_panel(ID3D11Device* device) {
#else
void draw_right_panel() {
#endif
#ifdef _WIN32
    (void)device;
#endif

#include "RightPanel/Actions/Primary.inl"
#include "RightPanel/Actions/HexAndSelection.inl"
#include "RightPanel/Preview/Load.inl"
#include "RightPanel/Export/Glb.inl"
#include "RightPanel/Filters/Extension.inl"
#include "RightPanel/Search/Global.inl"
#include "RightPanel/Content/LuaAndTable.inl"
#include "RightPanel/Pending/Model.inl"
#include "RightPanel/Pending/Texture.inl"
#include "RightPanel/Pending/Build.inl"
#include "RightPanel/Finalize.inl"

}
