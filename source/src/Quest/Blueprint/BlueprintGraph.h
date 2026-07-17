#pragma once

#include <string>
#include <vector>

#include "BlueprintTypes.h"

namespace Quest {
namespace Bp {

struct Pin {
    int         id = 0;
    std::string name;
    PinType     type = PinType::Exec;
    PinDir      dir = PinDir::Input;
    PinValue    value;
    bool        optional = false;
    bool        dynamic = false;   
};

struct Node {
    int         id = 0;
    std::string type;              
    float       x = 0.0f;
    float       y = 0.0f;
    std::string comment;
    std::string prop;              
    Quest::AuthoredNode prereq;    
    std::vector<Pin> pins;

    Pin*       FindPin(int pin_id);
    const Pin* FindPin(int pin_id) const;
    Pin*       FindPin(const char* name, PinDir dir);
    const Pin* FindPin(const char* name, PinDir dir) const;
    int        CountPins(PinDir dir, PinType type) const;
};

struct Link {
    int id = 0;
    int from_pin = 0;   
    int to_pin = 0;     
};

struct Variable {
    std::string name;
    PinType     type = PinType::Bool;
    PinValue    def;
};

struct BlueprintQuest {
    std::string quest_id;
    std::string quest_title;
    int         next_id = 1;       

    std::vector<Node>     nodes;
    std::vector<Link>     links;
    std::vector<Variable> variables;

    
    
    std::vector<Quest::AuthoredNode> prerequisites;
    int next_prerequisite_id = 1;

    uint64_t revision = 0;         

    int AllocId() { return next_id++; }

    Node*       NodeById(int node_id);
    const Node* NodeById(int node_id) const;
    Node*       NodeOfPin(int pin_id);
    const Node* NodeOfPin(int pin_id) const;
    Pin*        PinById(int pin_id);
    const Pin*  PinById(int pin_id) const;
    const Link* LinkById(int link_id) const;

    
    
    const Link* LinkInto(int input_pin_id) const;
    std::vector<const Link*> LinksFrom(int output_pin_id) const;
    std::vector<const Link*> LinksInto(int input_pin_id) const;
    bool IsPinLinked(int pin_id) const;
    int  ExecInDegree(int node_id) const;

    
    
    int  AddLink(int from_pin, int to_pin, std::string& reason);
    bool RemoveLink(int link_id);
    bool RemoveNode(int node_id);   

    void Touch() { ++revision; }
};



bool CanConnect(const BlueprintQuest& quest, int from_pin, int to_pin,
                std::string& reason);


bool ExecReaches(const BlueprintQuest& quest, int from_node, int to_node);

}
}
