#pragma once

#include <cstdint>
#include <string>

#include "Quest/QuestAuthoring.h"

namespace Quest {
namespace Bp {



enum class PinType : uint8_t {
    Exec,
    Bool,
    Number,
    String,
    TextTag,
    Entity,
    EntityDef,
    Item,
    Marker,
    Level,
    Vector3,
    Any,
    Prereq,   
};

enum class PinDir : uint8_t { Input, Output };




enum class NodeScope : uint8_t { Any, Quest, Entity };



struct PinValue {
    bool        b = false;
    double      num = 0.0;
    std::string str;
    Quest::WorldReference world;   
    Quest::ItemReference  item;    
    float       vec[3] = {0.0f, 0.0f, 0.0f};
};

enum class Severity : uint8_t { Error, Warning };

struct Diagnostic {
    int         node_id = 0;
    int         pin_id = 0;
    Severity    severity = Severity::Error;
    std::string message;
};

const char* PinTypeName(PinType type);


uint32_t PinTypeColor(PinType type);



bool PinTypesCompatible(PinType from, PinType to);

}
}
