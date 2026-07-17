#pragma once

#include "Quest/Blueprint/BlueprintGraph.h"

#include <imgui_node_editor.h>

#include <map>

namespace BlueprintUIDetail {

struct CanvasState {
    ax::NodeEditor::EditorContext* ctx = nullptr;
    bool layout_pending = true;     
    bool fit_pending = true;        
    int  selected_node = 0;
    int  pending_palette_pin = 0;   
    int  focus_node_request = 0;    
    float spawn_x = 0.0f;           
    float spawn_y = 0.0f;
    float palette_x = 0.0f;         
    float palette_y = 0.0f;

    
    
    
    int   place_topright_node = 0;
    float place_pos_x = 0.0f;
    float place_pos_y = 0.0f;

    
    std::map<int, int> node_diag;
};



void DrawCanvas(Quest::Bp::BlueprintQuest& quest, CanvasState& state);

void DestroyCanvas(CanvasState& state);

}
