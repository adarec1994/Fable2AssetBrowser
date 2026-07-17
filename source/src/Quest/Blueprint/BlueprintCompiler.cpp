#include "BlueprintCompiler.h"

#include "BlueprintNodeRegistry.h"

#include <algorithm>
#include <cstdio>
#include <sstream>
#include <unordered_map>

namespace Quest {
namespace Bp {

namespace {

std::string sanitize_identifier(const std::string& s) {
    std::string out;
    for (const char c : s) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '_';
        out.push_back(ok ? c : '_');
    }
    if (out.empty() || (out[0] >= '0' && out[0] <= '9')) {
        out.insert(out.begin(), '_');
    }
    return out;
}

bool pin_value_missing(const Pin& pin) {
    switch (pin.type) {
        case PinType::Entity:
        case PinType::Marker:
            return pin.value.world.entity_name.empty();
        case PinType::Level:
            return pin.value.world.level_id.empty() && pin.value.str.empty();
        case PinType::Item:
            return pin.value.item.internal_name.empty();
        case PinType::EntityDef:
            return pin.value.str.empty();
        default:
            return false;
    }
}


struct ChainOwnership {
    std::unordered_map<int, std::string> owner;   
    bool Assign(const BlueprintQuest& quest, const Node& start,
                const std::string& cls, CompileResult& result) {
        std::vector<int> stack{start.id};
        while (!stack.empty()) {
            const int id = stack.back();
            stack.pop_back();
            auto [it, inserted] = owner.emplace(id, cls);
            if (!inserted) {
                if (it->second != cls) {
                    result.diagnostics.push_back(
                        {id, 0, Severity::Error,
                         "This node is reachable from event chains on two "
                         "different threads (" + it->second + " and " + cls +
                         ") - duplicate it instead."});
                    return false;
                }
                continue;
            }
            const Node* node = quest.NodeById(id);
            if (!node) continue;
            for (const Pin& p : node->pins) {
                if (p.dir != PinDir::Output || p.type != PinType::Exec) {
                    continue;
                }
                for (const Link* l : quest.LinksFrom(p.id)) {
                    if (const Node* next = quest.NodeOfPin(l->to_pin)) {
                        stack.push_back(next->id);
                    }
                }
            }
        }
        return true;
    }
};

}

std::string LuaQuote(const std::string& s) {
    std::string out = "\"";
    for (const char c : s) {
        if (c == '\\' || c == '"') {
            out.push_back('\\');
            out.push_back(c);
        } else if (c == '\n') {
            out += "\\n";
        } else {
            out.push_back(c);
        }
    }
    out.push_back('"');
    return out;
}

std::string LuaNumber(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.10g", v);
    return buf;
}

void EmitContext::Line(const std::string& text) {
    if (!out) return;
    if (!text.empty()) {
        out->append((size_t)indent * 2, ' ');
        out->append(text);
    }
    out->push_back('\n');
}

void EmitContext::Open(const std::string& text) {
    Line(text);
    ++indent;
}

void EmitContext::Close(const std::string& text) {
    --indent;
    Line(text);
}

void EmitContext::Error(const Node& node, const std::string& message) {
    result->diagnostics.push_back({node.id, 0, Severity::Error, message});
}

void EmitContext::Warn(const Node& node, const std::string& message) {
    result->diagnostics.push_back({node.id, 0, Severity::Warning, message});
}

std::string EmitContext::TextTag(const Node& node, const Pin& pin,
                                 const std::string& text) {
    std::string tag = "TEXT_QUEST_" + quest_class + "_NODE_" +
                      std::to_string(node.id);
    
    int text_pins = 0;
    for (const Pin& p : node.pins) {
        if (p.dir == PinDir::Input &&
            (p.type == PinType::String || p.type == PinType::TextTag)) {
            ++text_pins;
        }
    }
    if (text_pins > 1) tag += "_" + std::to_string(pin.id);
    for (auto& entry : result->text_entries) {
        if (entry.first == tag) return tag;
    }
    result->text_entries.emplace_back(tag, text);
    return tag;
}

std::string EmitContext::StateField(const Node& node, const std::string& what,
                                    const std::string& init_expr) {
    const std::string field = what + "_" + std::to_string(node.id);
    (*init_fields)[current_class][field] = init_expr;
    return "self." + field;
}

std::string EmitContext::ExprForPin(const Pin& input_pin) {
    const Link* link = quest->LinkInto(input_pin.id);
    if (!link) {
        
        switch (input_pin.type) {
            case PinType::Bool:
                return input_pin.value.b ? "true" : "false";
            case PinType::Number:
                return LuaNumber(input_pin.value.num);
            case PinType::String:
            case PinType::TextTag:
                return LuaQuote(input_pin.value.str);
            case PinType::Level:
                return LuaQuote(input_pin.value.world.level_id.empty()
                                    ? input_pin.value.str
                                    : input_pin.value.world.level_id);
            case PinType::Item:
                return LuaQuote(input_pin.value.item.internal_name);
            case PinType::EntityDef:
                return LuaQuote(input_pin.value.str);
            case PinType::Entity:
                return "self:GetEntityWithName(" +
                       LuaQuote(input_pin.value.world.entity_name) + ")";
            case PinType::Marker:
                return "self:GetEntityWithName(" +
                       LuaQuote(input_pin.value.world.entity_name) +
                       ", \"marker\")";
            case PinType::Vector3: {
                std::ostringstream os;
                os << "CVector3(" << LuaNumber(input_pin.value.vec[0]) << ", "
                   << LuaNumber(input_pin.value.vec[1]) << ", "
                   << LuaNumber(input_pin.value.vec[2]) << ")";
                return os.str();
            }
            default:
                return "nil";
        }
    }

    const Pin* source = quest->PinById(link->from_pin);
    const Node* source_node = quest->NodeOfPin(link->from_pin);
    if (!source || !source_node) return "nil";
    const NodeDef* def = Registry::Find(source_node->type);
    if (!def) return "nil";

    std::string expr;
    if (def->pure) {
        if (expr_stack.count(source->id)) {
            Error(*source_node, "Data wires form a loop.");
            return "nil";
        }
        if (!def->emit_expr) {
            Error(*source_node,
                  "'" + def->title + "' has no expression emitter yet.");
            return "nil";
        }
        expr_stack.insert(source->id);
        expr = def->emit_expr(*this, *source_node, *source);
        expr_stack.erase(source->id);
    } else {
        
        expr = "self.Out_" + std::to_string(source->id);
    }

    
    if (source->type == PinType::Number &&
        (input_pin.type == PinType::String ||
         input_pin.type == PinType::TextTag)) {
        expr = "tostring(" + expr + ")";
    }
    return expr;
}

std::string EmitContext::Expr(const Node& node, const std::string& pin_name) {
    const Pin* pin = node.FindPin(pin_name.c_str(), PinDir::Input);
    if (!pin) return "nil";
    return ExprForPin(*pin);
}

void EmitContext::EmitNode(const Node& node, const Pin& entered) {
    const NodeDef* def = Registry::Find(node.type);
    if (!def) {
        Error(node, "Unknown node type '" + node.type + "'.");
        return;
    }
    if (!def->emit) {
        Error(node, "'" + def->title + "' has no code emitter yet.");
        return;
    }
    def->emit(*this, node, entered);
}

void EmitContext::ChainFromPin(const Pin& output_pin) {
    const auto links = quest->LinksFrom(output_pin.id);
    if (links.empty()) return;
    const Pin* entered = quest->PinById(links.front()->to_pin);
    const Node* next = quest->NodeOfPin(links.front()->to_pin);
    if (!entered || !next) return;

    if (quest->LinksInto(entered->id).size() > 1) {
        
        auto& emitted = (*emitted_segments)[current_class];
        auto& pending = (*pending_segments)[current_class];
        if (!emitted.count(entered->id) &&
            std::find(pending.begin(), pending.end(), entered->id) ==
                pending.end()) {
            pending.push_back(entered->id);
        }
        Line("self:Seg_" + std::to_string(entered->id) + "()");
        return;
    }
    EmitNode(*next, *entered);
}

void EmitContext::Chain(const Node& node, const std::string& pin_name) {
    for (const Pin& p : node.pins) {
        if (p.dir == PinDir::Output && p.type == PinType::Exec &&
            p.name == pin_name) {
            ChainFromPin(p);
            return;
        }
    }
}

namespace {

void validate(const BlueprintQuest& quest, CompileResult& result,
              std::vector<const Node*>& events) {
    Registry::EnsureRegistered();

    for (const Node& node : quest.nodes) {
        const NodeDef* def = Registry::Find(node.type);
        if (!def) {
            result.diagnostics.push_back(
                {node.id, 0, Severity::Error,
                 "Unknown node type '" + node.type + "'."});
            continue;
        }
        if (def->is_event) events.push_back(&node);

        for (const Pin& pin : node.pins) {
            if (pin.dir != PinDir::Input || pin.type == PinType::Exec) {
                continue;
            }
            if (pin.optional) continue;
            if (!quest.LinkInto(pin.id) && pin_value_missing(pin)) {
                result.diagnostics.push_back(
                    {node.id, pin.id, Severity::Error,
                     "'" + def->title + "' needs a value for '" + pin.name +
                     "'."});
            }
        }

        
        if (!def->is_event && !def->pure) {
            bool any_exec_in_linked = false;
            bool has_exec_in = false;
            for (const Pin& pin : node.pins) {
                if (pin.dir == PinDir::Input && pin.type == PinType::Exec) {
                    has_exec_in = true;
                    if (!quest.LinksInto(pin.id).empty()) {
                        any_exec_in_linked = true;
                    }
                }
            }
            if (has_exec_in && !any_exec_in_linked) {
                result.diagnostics.push_back(
                    {node.id, 0, Severity::Warning,
                     "'" + def->title + "' is never executed."});
            }
        }
    }

    if (events.empty()) {
        result.diagnostics.push_back(
            {0, 0, Severity::Error,
             "Add an event node (e.g. On Quest Start) to begin the quest."});
    }

    std::set<std::string> var_names;
    for (const Variable& var : quest.variables) {
        const std::string clean = sanitize_identifier(var.name);
        if (var.name.empty() || clean != var.name) {
            result.diagnostics.push_back(
                {0, 0, Severity::Error,
                 "Variable '" + var.name +
                 "' must use letters, digits and underscores."});
        }
        if (!var_names.insert(var.name).second) {
            result.diagnostics.push_back(
                {0, 0, Severity::Error,
                 "Duplicate variable name '" + var.name + "'."});
        }
    }

    std::sort(events.begin(), events.end(),
              [](const Node* a, const Node* b) { return a->id < b->id; });
}


std::string event_entity_class(const Node& event, CompileResult& result) {
    const Pin* entity = event.FindPin("Entity", PinDir::Input);
    if (!entity || entity->value.world.entity_name.empty()) {
        result.diagnostics.push_back(
            {event.id, entity ? entity->id : 0, Severity::Error,
             "This event needs a level entity to watch."});
        return {};
    }
    return sanitize_identifier(entity->value.world.entity_name);
}

struct ChainBody {
    const Node* event = nullptr;
    std::string lua;
};

struct ClassChains {
    std::string cls;
    std::string entity_name;          
    std::vector<ChainBody> chains;
};

void emit_segments(EmitContext& ctx, const std::string& cls,
                   std::string& out_methods) {
    auto& pending = (*ctx.pending_segments)[cls];
    auto& emitted = (*ctx.emitted_segments)[cls];
    while (!pending.empty()) {
        const int pin_id = pending.front();
        pending.erase(pending.begin());
        if (!emitted.insert(pin_id).second) continue;
        const Pin* entered = ctx.quest->PinById(pin_id);
        const Node* node = ctx.quest->NodeOfPin(pin_id);
        if (!entered || !node) continue;

        std::string body;
        ctx.out = &body;
        ctx.indent = 1;
        ctx.EmitNode(*node, *entered);

        out_methods += "function " + cls + ":Seg_" +
                       std::to_string(pin_id) + "()\n" + body + "end\n\n";
    }
    ctx.out = nullptr;
}

void emit_update_body(EmitContext& ctx, const ClassChains& cc,
                      const char* fn_name, const std::string& prologue,
                      std::string& out) {
    out += "function " + cc.cls + ":" + fn_name + "()\n";
    out += prologue;
    if (cc.chains.size() == 1) {
        out += cc.chains.front().lua;
    } else {
        out += "  local chains = {\n";
        for (const ChainBody& chain : cc.chains) {
            out += "    function() self:Chain_" +
                   std::to_string(chain.event->id) + "() end,\n";
        }
        out += "  }\n"
               "  local cos = {}\n"
               "  for i = 1, #chains do cos[i] = coroutine.create(chains[i]) end\n"
               "  while true do\n"
               "    local alive = false\n"
               "    for i = 1, #cos do\n"
               "      if coroutine.status(cos[i]) ~= \"dead\" then\n"
               "        alive = true\n"
               "        local ok, err = coroutine.resume(cos[i])\n"
               "        if not ok then\n"
               "          Debug.Error(\"" + ctx.quest_class +
               " chain error: \" .. tostring(err))\n"
               "        end\n"
               "      end\n"
               "    end\n"
               "    if not alive then return end\n"
               "    coroutine.yield()\n"
               "  end\n";
    }
    out += "end\n\n";
    (void)ctx;
}

}

std::vector<Quest::AuthoredNode> CollectPrerequisites(
    const BlueprintQuest& quest) {
    std::vector<Quest::AuthoredNode> out;
    std::vector<const Node*> sources;
    for (const Node& node : quest.nodes) {
        if (node.type != "event.quest_start") continue;
        const Pin* pin = node.FindPin("Prerequisites", PinDir::Input);
        if (!pin) continue;
        for (const Link* link : quest.LinksInto(pin->id)) {
            if (const Node* source = quest.NodeOfPin(link->from_pin)) {
                if (source->type.rfind("prereq.", 0) == 0) {
                    sources.push_back(source);
                }
            }
        }
    }
    std::sort(sources.begin(), sources.end(),
              [](const Node* a, const Node* b) { return a->id < b->id; });
    for (const Node* source : sources) {
        out.push_back(source->prereq);
    }
    out.insert(out.end(), quest.prerequisites.begin(),
               quest.prerequisites.end());
    return out;
}

CompileResult Compile(const BlueprintQuest& quest) {
    CompileResult result;
    std::vector<const Node*> events;
    validate(quest, result, events);
    if (result.HasErrors()) return result;

    const std::string quest_class = sanitize_identifier(quest.quest_id);

    result.text_entries.emplace_back(
        "Quest_" + quest.quest_id,
        quest.quest_title.empty() ? quest.quest_id : quest.quest_title);

    EmitContext ctx;
    ctx.quest = &quest;
    ctx.result = &result;
    ctx.quest_class = quest_class;

    std::map<std::string, std::map<std::string, std::string>> init_fields;
    std::map<std::string, std::vector<int>> pending_segments;
    std::map<std::string, std::set<int>> emitted_segments;
    ctx.init_fields = &init_fields;
    ctx.pending_segments = &pending_segments;
    ctx.emitted_segments = &emitted_segments;

    
    std::vector<ClassChains> classes;
    classes.push_back({quest_class, "", {}});
    ChainOwnership ownership;

    for (const Node* event : events) {
        const NodeDef* def = Registry::Find(event->type);
        std::string cls = quest_class;
        std::string entity_name;
        if (def->scope == NodeScope::Entity) {
            const Pin* entity = event->FindPin("Entity", PinDir::Input);
            entity_name = entity ? entity->value.world.entity_name
                                 : std::string();
            cls = event_entity_class(*event, result);
            if (cls.empty()) continue;
        }
        if (!ownership.Assign(quest, *event, cls, result)) continue;

        ClassChains* target = nullptr;
        for (ClassChains& cc : classes) {
            if (cc.cls == cls) target = &cc;
        }
        if (!target) {
            classes.push_back({cls, entity_name, {}});
            target = &classes.back();
        }
        target->chains.push_back({event, {}});
    }
    if (result.HasErrors()) return result;

    
    for (ClassChains& cc : classes) {
        ctx.current_class = cc.cls;
        ctx.entity_scope = cc.cls != quest_class;
        for (ChainBody& chain : cc.chains) {
            std::string body;
            ctx.out = &body;
            ctx.indent = 1;
            const Pin* exec_out = nullptr;
            for (const Pin& p : chain.event->pins) {
                if (p.dir == PinDir::Output && p.type == PinType::Exec) {
                    exec_out = &p;
                    break;
                }
            }
            ctx.EmitNode(*chain.event, *exec_out);
            ctx.out = nullptr;
            chain.lua = body;
        }
    }
    if (result.HasErrors()) return result;

    
    
    
    
    
    std::string quest_update_prologue;
    if (classes.size() > 1) {
        quest_update_prologue +=
            "  if self.ScriptEntityNames == nil then "
            "self.ScriptEntityNames = {} end\n";
        for (const ClassChains& cc : classes) {
            if (cc.cls == quest_class) continue;
            quest_update_prologue +=
                "  if self.ScriptEntityNames[" +
                LuaQuote(cc.entity_name) + "] ~= " + cc.cls + " then\n"
                "    self:StartNewEntityThread(" +
                LuaQuote(cc.entity_name) + ", " + cc.cls + ")\n"
                "  end\n";
        }
    }

    std::map<std::string, std::string> class_methods;
    for (ClassChains& cc : classes) {
        ctx.current_class = cc.cls;
        ctx.entity_scope = cc.cls != quest_class;
        std::string methods;
        if (cc.chains.size() > 1) {
            for (const ChainBody& chain : cc.chains) {
                methods += "function " + cc.cls + ":Chain_" +
                           std::to_string(chain.event->id) + "()\n" +
                           chain.lua + "end\n\n";
            }
        }
        const char* fn = cc.cls == quest_class ? "Update" : "CustomUpdate";
        if (cc.chains.empty()) {
            
            
            methods += "function " + cc.cls + ":" + std::string(fn) +
                       "()\n";
            if (cc.cls == quest_class) {
                methods += quest_update_prologue;
            }
            methods += "  while true do\n    coroutine.yield()\n"
                       "  end\nend\n\n";
        } else {
            emit_update_body(ctx, cc, fn,
                             cc.cls == quest_class
                                 ? quest_update_prologue
                                 : std::string(),
                             methods);
        }
        emit_segments(ctx, cc.cls, methods);
        class_methods[cc.cls] = std::move(methods);
    }
    if (result.HasErrors()) return result;

    
    std::ostringstream lua;
    lua << "module(..., package.seeall)\n"
        << "QuestManager.NewQuestThread(" << LuaQuote(quest.quest_id)
        << ")\n\n";

    
    lua << "function " << quest_class << ":Init()\n"
        << "  self.QuestName = " << LuaQuote(quest.quest_id) << "\n";
    for (const Variable& var : quest.variables) {
        std::string init = "nil";
        switch (var.type) {
            case PinType::Bool: init = var.def.b ? "true" : "false"; break;
            case PinType::Number: init = LuaNumber(var.def.num); break;
            case PinType::String: init = LuaQuote(var.def.str); break;
            default: break;
        }
        lua << "  self.Var_" << var.name << " = " << init << "\n";
    }
    for (const auto& [field, init] : init_fields[quest_class]) {
        lua << "  self." << field << " = " << init << "\n";
    }
    for (const ClassChains& cc : classes) {
        if (cc.cls == quest_class) continue;
        lua << "  self:StartNewEntityThread(" << LuaQuote(cc.entity_name)
            << ", " << cc.cls << ")\n";
    }
    lua << "  QuestTracker.Register(QuestManager.HeroEntity, self.QuestName, "
        << LuaQuote("Quest_" + quest.quest_id) << ")\n"
        << "  QuestTracker.Unlock(QuestManager.HeroEntity, self.QuestName)\n"
        << "end\n\n";

    lua << class_methods[quest_class];

    
    for (ClassChains& cc : classes) {
        if (cc.cls == quest_class) continue;
        lua << "QuestManager.NewEntityThread(" << LuaQuote(cc.cls) << ")\n\n";
        lua << "function " << cc.cls << ":Init()\n";
        for (const auto& [field, init] : init_fields[cc.cls]) {
            lua << "  self." << field << " = " << init << "\n";
        }
        lua << "end\n\n";
        lua << class_methods[cc.cls];
    }

    result.quest_lua = lua.str();
    return result;
}

}
}
