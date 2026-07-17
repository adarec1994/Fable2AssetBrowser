#pragma once

#include <functional>
#include <string>
#include <vector>

#include "BlueprintGraph.h"

namespace Quest {
namespace Bp {

struct EmitContext;   



struct PinSpec {
    const char* name;
    PinType     type;
    PinDir      dir;
    const char* default_value = "";   
    bool        optional = false;
};

struct NodeDef {
    std::string type;       
    std::string title;      
    std::string category;   
    std::string icon;       
    uint32_t    header_color = 0xFF3D4148;   
    NodeScope   scope = NodeScope::Any;
    bool        is_event = false;
    bool        pure = false;      
    bool        latent = false;    
    bool        dynamic_outputs = false;   
    std::vector<PinSpec> pins;

    
    
    
    std::function<void(EmitContext&, const Node&, const Pin& entered)> emit;

    
    
    std::function<std::string(EmitContext&, const Node&, const Pin&)> emit_expr;
};

namespace Registry {


void EnsureRegistered();

void Register(NodeDef def);

const NodeDef* Find(const std::string& type);
const std::vector<NodeDef>& All();
std::vector<std::string> Categories();



int Instantiate(BlueprintQuest& quest, const std::string& type,
                float x, float y);



int AddDynamicExecOutput(BlueprintQuest& quest, Node& node);




void SyncVariableNode(BlueprintQuest& quest, Node& node);

}


void RegisterEventNodes();
void RegisterFlowNodes();
void RegisterDataNodes();
void RegisterQuestNodes();
void RegisterDialogueNodes();
void RegisterInventoryNodes();
void RegisterEntityNodes();
void RegisterWorldNodes();
void RegisterUtilNodes();
void RegisterPrereqNodes();
void RegisterGameVarNodes();

}
}
