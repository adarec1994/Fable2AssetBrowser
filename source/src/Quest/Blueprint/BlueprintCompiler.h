#pragma once

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "BlueprintGraph.h"

namespace Quest {
namespace Bp {

struct CompileResult {
    std::string quest_lua;
    std::vector<std::pair<std::string, std::string>> text_entries;
    std::vector<Diagnostic> diagnostics;

    bool HasErrors() const {
        for (const Diagnostic& d : diagnostics) {
            if (d.severity == Severity::Error) return true;
        }
        return false;
    }
};




std::vector<Quest::AuthoredNode> CollectPrerequisites(
    const BlueprintQuest& quest);







CompileResult Compile(const BlueprintQuest& quest);


struct EmitContext {
    const BlueprintQuest* quest = nullptr;
    CompileResult*        result = nullptr;

    std::string quest_class;      
    std::string current_class;    
    bool        entity_scope = false;

    std::string* out = nullptr;
    int          indent = 1;

    
    std::map<std::string, std::map<std::string, std::string>>* init_fields =
        nullptr;

    
    
    std::map<std::string, std::vector<int>>* pending_segments = nullptr;
    std::map<std::string, std::set<int>>*    emitted_segments = nullptr;

    std::set<int> expr_stack;     

    void Line(const std::string& text);
    void Open(const std::string& text);    
    void Close(const std::string& text);   

    
    
    std::string Expr(const Node& node, const std::string& pin_name);
    std::string ExprForPin(const Pin& input_pin);

    
    void Chain(const Node& node, const std::string& pin_name);
    void ChainFromPin(const Pin& output_pin);

    
    void EmitNode(const Node& node, const Pin& entered);

    
    std::string TextTag(const Node& node, const Pin& pin,
                        const std::string& text);

    
    std::string StateField(const Node& node, const std::string& what,
                           const std::string& init_expr);

    
    std::string QuestSelf() const {
        return entity_scope ? "self.ParentQuest" : "self";
    }

    void Error(const Node& node, const std::string& message);
    void Warn(const Node& node, const std::string& message);
};

std::string LuaQuote(const std::string& s);
std::string LuaNumber(double v);

}
}
