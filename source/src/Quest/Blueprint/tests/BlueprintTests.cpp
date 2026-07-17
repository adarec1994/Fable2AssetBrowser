#include "Quest/Blueprint/BlueprintCompiler.h"
#include "Quest/Blueprint/BlueprintGraph.h"
#include "Quest/Blueprint/BlueprintNodeRegistry.h"
#include "Quest/Blueprint/BlueprintSerialize.h"

#include <cassert>
#include <cstdlib>

#include <iostream>




#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::cerr << "CHECK FAILED " << __FILE__ << ":" << __LINE__ \
                      << ": " << #cond << "\n";                         \
            std::abort();                                               \
        }                                                               \
    } while (0)

#include <set>
#include <string>

namespace {

using namespace Quest::Bp;

int pin_id(const BlueprintQuest& quest, int node_id, const char* name,
           PinDir dir) {
    const Node* node = quest.NodeById(node_id);
    CHECK(node && "node exists");
    const Pin* pin = node->FindPin(name, dir);
    CHECK(pin && "pin exists");
    return pin->id;
}

void test_registry_integrity() {
    Registry::EnsureRegistered();
    const auto& defs = Registry::All();
    CHECK(!defs.empty());

    std::set<std::string> keys;
    for (const NodeDef& def : defs) {
        CHECK(!def.type.empty());
        CHECK(keys.insert(def.type).second && "unique type keys");
        CHECK(!def.title.empty());
        CHECK(!def.category.empty());
        CHECK(!def.pins.empty());

        int exec_out = 0;
        int exec_in = 0;
        for (const PinSpec& spec : def.pins) {
            if (spec.type == PinType::Exec) {
                if (spec.dir == PinDir::Output) ++exec_out;
                else ++exec_in;
            }
        }
        if (def.is_event) {
            CHECK(exec_out >= 1 && "events start an exec chain");
            CHECK(exec_in == 0 && "events have no exec input");
        }
        if (def.pure) {
            CHECK(exec_out == 0 && exec_in == 0 &&
                   "pure nodes have no exec pins");
        }
        if (def.is_event) {
            CHECK(def.scope != NodeScope::Any &&
                   "events declare their host thread");
        }
        if (def.pure) {
            CHECK(def.emit_expr && "pure nodes need an expression emitter");
        } else {
            CHECK(def.emit && "impure nodes need a statement emitter");
        }
    }
    std::cout << "registry integrity: " << defs.size() << " node defs OK\n";
}

void test_instantiate() {
    BlueprintQuest quest;
    quest.quest_id = "QB_Test";

    const int branch = Registry::Instantiate(quest, "flow.branch", 10, 20);
    CHECK(branch != 0);
    const Node* node = quest.NodeById(branch);
    CHECK(node);
    CHECK(node->pins.size() == 4);
    CHECK(node->CountPins(PinDir::Input, PinType::Exec) == 1);
    CHECK(node->CountPins(PinDir::Input, PinType::Bool) == 1);
    CHECK(node->CountPins(PinDir::Output, PinType::Exec) == 2);

    std::set<int> ids;
    ids.insert(node->id);
    for (const Pin& p : node->pins) {
        CHECK(ids.insert(p.id).second && "ids unique");
    }
    CHECK(quest.next_id > branch);

    
    const int near = Registry::Instantiate(quest, "event.hero_near", 0, 0);
    const Node* near_node = quest.NodeById(near);
    CHECK(near_node);
    const Pin* radius = near_node->FindPin("Radius", PinDir::Input);
    CHECK(radius && radius->value.num == 3.0);
    const Pin* repeat = near_node->FindPin("Repeat", PinDir::Input);
    CHECK(repeat && repeat->value.b);

    CHECK(Registry::Instantiate(quest, "no.such.node", 0, 0) == 0);
    std::cout << "instantiate OK\n";
}

void test_link_rules() {
    BlueprintQuest quest;
    std::string reason;

    const int start = Registry::Instantiate(quest, "event.quest_start", 0, 0);
    const int branch = Registry::Instantiate(quest, "flow.branch", 0, 0);
    const int delay = Registry::Instantiate(quest, "flow.delay", 0, 0);
    const int flip = Registry::Instantiate(quest, "flow.flip_flop", 0, 0);

    const int start_out = pin_id(quest, start, "", PinDir::Output);
    const int branch_in = pin_id(quest, branch, "", PinDir::Input);
    const int branch_cond = pin_id(quest, branch, "Condition", PinDir::Input);
    const int branch_true = pin_id(quest, branch, "True", PinDir::Output);
    const int delay_in = pin_id(quest, delay, "", PinDir::Input);
    const int delay_seconds = pin_id(quest, delay, "Seconds", PinDir::Input);
    const int flip_is_a = pin_id(quest, flip, "Is A", PinDir::Output);

    
    CHECK(quest.AddLink(start_out, branch_in, reason) != 0);
    CHECK(quest.links.size() == 1);

    
    CHECK(quest.AddLink(delay_in, branch_true, reason) != 0);

    
    CHECK(quest.AddLink(start_out, delay_in, reason) != 0);
    CHECK(quest.LinksFrom(start_out).size() == 1);
    CHECK(quest.LinkInto(branch_in) == nullptr && "old link replaced");

    
    const int branch_true_link = quest.AddLink(branch_true, delay_in, reason);
    CHECK(branch_true_link != 0);
    CHECK(quest.LinksInto(delay_in).size() == 2);

    
    CHECK(quest.AddLink(flip_is_a, branch_cond, reason) != 0);
    CHECK(quest.AddLink(flip_is_a, branch_cond, reason) != 0);
    CHECK(quest.LinksInto(branch_cond).size() == 1);

    
    reason.clear();
    CHECK(quest.AddLink(flip_is_a, delay_seconds, reason) == 0);
    CHECK(!reason.empty());

    
    CHECK(quest.AddLink(start_out, branch_cond, reason) == 0);

    
    CHECK(quest.AddLink(branch_true, branch_in, reason) == 0);

    
    
    const int delay_out = pin_id(quest, delay, "", PinDir::Output);
    reason.clear();
    CHECK(quest.AddLink(delay_out, branch_in, reason) == 0 &&
           "cycle rejected");
    CHECK(!reason.empty());

    
    
    CHECK(quest.RemoveLink(branch_true_link));
    CHECK(quest.AddLink(delay_out, branch_in, reason) != 0);

    std::cout << "link rules OK\n";
}

void test_remove_node() {
    BlueprintQuest quest;
    std::string reason;
    const int start = Registry::Instantiate(quest, "event.quest_start", 0, 0);
    const int delay = Registry::Instantiate(quest, "flow.delay", 0, 0);
    const int start_out = pin_id(quest, start, "", PinDir::Output);
    const int delay_in = pin_id(quest, delay, "", PinDir::Input);
    CHECK(quest.AddLink(start_out, delay_in, reason) != 0);
    CHECK(quest.links.size() == 1);

    CHECK(quest.RemoveNode(delay));
    CHECK(quest.NodeById(delay) == nullptr);
    CHECK(quest.links.empty() && "links to removed node dropped");
    CHECK(!quest.RemoveNode(delay));
    std::cout << "remove node OK\n";
}

void test_dynamic_sequence_pins() {
    BlueprintQuest quest;
    const int seq = Registry::Instantiate(quest, "flow.sequence", 0, 0);
    Node* node = quest.NodeById(seq);
    CHECK(node);
    CHECK(node->CountPins(PinDir::Output, PinType::Exec) == 2);

    const int new_pin = Registry::AddDynamicExecOutput(quest, *node);
    CHECK(new_pin != 0);
    CHECK(node->CountPins(PinDir::Output, PinType::Exec) == 3);
    CHECK(node->FindPin("Then 2", PinDir::Output) != nullptr);
    CHECK(node->FindPin("Then 2", PinDir::Output)->dynamic);

    
    const int branch = Registry::Instantiate(quest, "flow.branch", 0, 0);
    Node* branch_node = quest.NodeById(branch);
    CHECK(Registry::AddDynamicExecOutput(quest, *branch_node) == 0);
    std::cout << "dynamic sequence pins OK\n";
}

void test_exec_in_degree() {
    BlueprintQuest quest;
    std::string reason;
    const int start = Registry::Instantiate(quest, "event.quest_start", 0, 0);
    const int flip = Registry::Instantiate(quest, "flow.flip_flop", 0, 0);
    const int delay = Registry::Instantiate(quest, "flow.delay", 0, 0);

    const int start_out = pin_id(quest, start, "", PinDir::Output);
    const int flip_in = pin_id(quest, flip, "", PinDir::Input);
    const int flip_a = pin_id(quest, flip, "A", PinDir::Output);
    const int flip_b = pin_id(quest, flip, "B", PinDir::Output);
    const int delay_in = pin_id(quest, delay, "", PinDir::Input);

    CHECK(quest.AddLink(start_out, flip_in, reason) != 0);
    CHECK(quest.AddLink(flip_a, delay_in, reason) != 0);
    CHECK(quest.AddLink(flip_b, delay_in, reason) != 0);

    CHECK(quest.ExecInDegree(delay) == 2 && "reconvergent flow detected");
    CHECK(quest.ExecInDegree(flip) == 1);
    CHECK(quest.ExecInDegree(start) == 0);
    std::cout << "exec in-degree OK\n";
}

Pin& mutable_pin(BlueprintQuest& quest, int node_id, const char* name,
                 PinDir dir) {
    Node* node = quest.NodeById(node_id);
    CHECK(node);
    Pin* pin = node->FindPin(name, dir);
    CHECK(pin);
    return *pin;
}

void link(BlueprintQuest& quest, int from_node, const char* from_pin,
          int to_node, const char* to_pin) {
    std::string reason;
    const int a = pin_id(quest, from_node, from_pin, PinDir::Output);
    const int b = pin_id(quest, to_node, to_pin, PinDir::Input);
    const int id = quest.AddLink(a, b, reason);
    CHECK(id != 0 && "test link connects");
    (void)id;
}




BlueprintQuest build_fetch_quest() {
    BlueprintQuest quest;
    quest.quest_id = "QB100_Fetch";
    quest.quest_title = "The Fetcher";

    const int start = Registry::Instantiate(quest, "event.quest_start", 0, 0);
    const int objective =
        Registry::Instantiate(quest, "quest.set_objective_text", 0, 0);
    const int wait = Registry::Instantiate(quest, "flow.wait_until", 0, 0);
    const int has = Registry::Instantiate(quest, "inv.has_item", 0, 0);
    const int say = Registry::Instantiate(quest, "dialogue.say_line", 0, 0);
    const int gold = Registry::Instantiate(quest, "inv.modify_gold", 0, 0);
    const int done = Registry::Instantiate(quest, "quest.complete", 0, 0);

    mutable_pin(quest, objective, "Text", PinDir::Input).value.str =
        "Collect 5 apples";
    mutable_pin(quest, has, "Item", PinDir::Input).value.item.internal_name =
        "ObjectInventoryFoodApple";
    mutable_pin(quest, has, "Count", PinDir::Input).value.num = 5;
    mutable_pin(quest, say, "Speaker", PinDir::Input)
        .value.world.entity_name = "AppleFarmer";
    mutable_pin(quest, say, "Text", PinDir::Input).value.str =
        "Wonderful apples!";
    mutable_pin(quest, gold, "Amount", PinDir::Input).value.num = 250;

    link(quest, start, "", objective, "");
    link(quest, objective, "", wait, "");
    std::string reason;
    CHECK(quest.AddLink(pin_id(quest, has, "Result", PinDir::Output),
                         pin_id(quest, wait, "Condition", PinDir::Input),
                         reason) != 0);
    link(quest, wait, "", say, "");
    link(quest, say, "", gold, "");
    link(quest, gold, "", done, "");
    return quest;
}

void test_compile_fetch_quest() {
    BlueprintQuest quest = build_fetch_quest();
    const CompileResult result = Compile(quest);
    for (const Diagnostic& d : result.diagnostics) {
        std::cout << "  diag: " << d.message << "\n";
    }
    CHECK(!result.HasErrors());
    const std::string& lua = result.quest_lua;

    auto contains = [&](const char* needle) {
        const bool found = lua.find(needle) != std::string::npos;
        if (!found) {
            std::cout << "MISSING: " << needle << "\n---\n" << lua << "---\n";
        }
        return found;
    };
    CHECK(contains("module(..., package.seeall)"));
    CHECK(contains("QuestManager.NewQuestThread(\"QB100_Fetch\")"));
    CHECK(contains("QuestTracker.Register(QuestManager.HeroEntity, "
                    "self.QuestName, \"Quest_QB100_Fetch\")"));
    CHECK(contains("function QB100_Fetch:Update()"));
    CHECK(contains("QuestTracker.SetObjectiveTag(QuestManager.HeroEntity, "
                    "self.QuestName, \"TEXT_QUEST_QB100_Fetch_NODE_"));
    CHECK(contains("while not ((Inventory.GetNumberOfItemsOfType("
                    "QuestManager.HeroEntity, "
                    "\"ObjectInventoryFoodApple\") >= 5)) do"));
    CHECK(contains("coroutine.yield()"));
    CHECK(contains("ScriptFunction.SaySimLine(self:GetEntityWithName("
                    "\"AppleFarmer\"), \"TEXT_QUEST_QB100_Fetch_NODE_"));
    CHECK(contains("Money.Modify(QuestManager.HeroEntity, 250)"));
    CHECK(contains("QuestTracker.SetAsCompleted(QuestManager.HeroEntity, "
                    "self.QuestName, true, true)"));

    
    CHECK(result.text_entries.size() == 3);
    CHECK(result.text_entries[0].first == "Quest_QB100_Fetch");
    CHECK(result.text_entries[0].second == "The Fetcher");

    
    const CompileResult again = Compile(quest);
    CHECK(again.quest_lua == lua);
    std::cout << "compile fetch quest OK (" << lua.size() << " bytes)\n";
}

void test_compile_branch_and_reconvergence() {
    BlueprintQuest quest;
    quest.quest_id = "QB101_Branchy";

    const int start = Registry::Instantiate(quest, "event.quest_start", 0, 0);
    const int branch = Registry::Instantiate(quest, "flow.branch", 0, 0);
    const int gold_a = Registry::Instantiate(quest, "inv.modify_gold", 0, 0);
    const int gold_b = Registry::Instantiate(quest, "inv.modify_gold", 0, 0);
    const int done = Registry::Instantiate(quest, "quest.complete", 0, 0);

    mutable_pin(quest, branch, "Condition", PinDir::Input).value.b = true;
    mutable_pin(quest, gold_a, "Amount", PinDir::Input).value.num = 10;
    mutable_pin(quest, gold_b, "Amount", PinDir::Input).value.num = 20;

    link(quest, start, "", branch, "");
    link(quest, branch, "True", gold_a, "");
    link(quest, branch, "False", gold_b, "");
    
    link(quest, gold_a, "", done, "");
    link(quest, gold_b, "", done, "");

    const CompileResult result = Compile(quest);
    CHECK(!result.HasErrors());
    const std::string& lua = result.quest_lua;

    
    const size_t seg_def = lua.find(":Seg_");
    CHECK(seg_def != std::string::npos);
    size_t calls = 0;
    for (size_t at = lua.find("self:Seg_"); at != std::string::npos;
         at = lua.find("self:Seg_", at + 1)) {
        ++calls;
    }
    CHECK(calls == 2);
    CHECK(lua.find("if true then") != std::string::npos);
    std::cout << "branch + reconvergence hoisting OK\n";
}

void test_compile_entity_event_thread() {
    BlueprintQuest quest;
    quest.quest_id = "QB102_Door";

    const int near = Registry::Instantiate(quest, "event.interacted", 0, 0);
    const int say = Registry::Instantiate(quest, "dialogue.say_line", 0, 0);
    mutable_pin(quest, near, "Entity", PinDir::Input)
        .value.world.entity_name = "GravekeeperFrontDoor";
    mutable_pin(quest, say, "Speaker", PinDir::Input)
        .value.world.entity_name = "Victor";
    mutable_pin(quest, say, "Text", PinDir::Input).value.str = "Hello.";
    link(quest, near, "", say, "");

    const CompileResult result = Compile(quest);
    CHECK(!result.HasErrors());
    const std::string& lua = result.quest_lua;
    CHECK(lua.find("QuestManager.NewEntityThread(\"GravekeeperFrontDoor\")")
           != std::string::npos);
    CHECK(lua.find("self:StartNewEntityThread(\"GravekeeperFrontDoor\", "
                   "GravekeeperFrontDoor)") != std::string::npos);
    CHECK(lua.find("if self.ScriptEntityNames[\"GravekeeperFrontDoor\"] ~= "
                   "GravekeeperFrontDoor then") != std::string::npos);
    CHECK(lua.find("function GravekeeperFrontDoor:CustomUpdate()") !=
           std::string::npos);
    CHECK(lua.find("while not self.Interacted do") != std::string::npos);
    
    CHECK(lua.find("function QB102_Door:Update()") != std::string::npos);
    CHECK(lua.find("  while true do\n    coroutine.yield()") !=
          std::string::npos);
    std::cout << "entity event thread OK\n";
}

void test_compile_static_prop_hold_prompt() {
    BlueprintQuest quest;
    quest.quest_id = "QB104_StaticPrompt";
    quest.quest_title = "Static Prompt";

    const int near = Registry::Instantiate(quest, "event.hero_near", 0, 0);
    const int offer =
        Registry::Instantiate(quest, "dialogue.offer_quest", 0, 0);
    mutable_pin(quest, near, "Entity", PinDir::Input)
        .value.world.entity_name = "F2AB_Static_TestProp";
    mutable_pin(quest, near, "Radius", PinDir::Input).value.num = 8;
    mutable_pin(quest, offer, "Text", PinDir::Input).value.str =
        "Hold A to activate";
    link(quest, near, "Triggered", offer, "");

    const CompileResult result = Compile(quest);
    CHECK(!result.HasErrors());
    const std::string& lua = result.quest_lua;
    CHECK(lua.find("self:StartNewEntityThread(\"F2AB_Static_TestProp\", "
                   "F2AB_Static_TestProp)") != std::string::npos);
    CHECK(lua.find("if self.ScriptEntityNames[\"F2AB_Static_TestProp\"] ~= "
                   "F2AB_Static_TestProp then") != std::string::npos);
    CHECK(lua.find("self.BpInteractionRadius = 8") != std::string::npos);
    CHECK(lua.find("GUI.DisplayInfoBoxParams({") != std::string::npos);
    CHECK(lua.find("IsHoldAButton = true") != std::string::npos);
    CHECK(lua.find("MESSAGE_EVENT_INFOBOX") != std::string::npos);
    CHECK(lua.find("ShowToasterAcceptBoxWithDialogueUntilCondition") ==
          std::string::npos);
    std::cout << "static prop hold prompt OK\n";
}

void test_compile_validation_errors() {
    BlueprintQuest quest;
    quest.quest_id = "QB103_Broken";

    
    CompileResult result = Compile(quest);
    CHECK(result.HasErrors());

    
    const int start = Registry::Instantiate(quest, "event.quest_start", 0, 0);
    const int say = Registry::Instantiate(quest, "dialogue.say_line", 0, 0);
    link(quest, start, "", say, "");
    result = Compile(quest);
    CHECK(result.HasErrors());

    
    mutable_pin(quest, say, "Speaker", PinDir::Input)
        .value.world.entity_name = "Bob";
    const int orphan = Registry::Instantiate(quest, "inv.modify_gold", 0, 0);
    (void)orphan;
    result = Compile(quest);
    CHECK(!result.HasErrors());
    bool has_warning = false;
    for (const Diagnostic& d : result.diagnostics) {
        if (d.severity == Severity::Warning) has_warning = true;
    }
    CHECK(has_warning);
    std::cout << "validation diagnostics OK\n";
}

void test_serialize_round_trip() {
    BlueprintQuest quest = build_fetch_quest();
    quest.variables.push_back({"GateOpen", PinType::Bool, {}});
    quest.Touch();

    const std::string json = SerializeToString(quest);
    BlueprintQuest restored;
    std::string error;
    CHECK(DeserializeFromString(json, restored, error));
    CHECK(restored.quest_id == quest.quest_id);
    CHECK(restored.nodes.size() == quest.nodes.size());
    CHECK(restored.links.size() == quest.links.size());
    CHECK(restored.variables.size() == quest.variables.size());
    CHECK(restored.next_id == quest.next_id);

    
    const CompileResult a = Compile(quest);
    const CompileResult b = Compile(restored);
    CHECK(a.quest_lua == b.quest_lua);
    CHECK(SerializeToString(restored) == json);

    
    BlueprintQuest bad;
    CHECK(!DeserializeFromString("not json at all", bad, error));
    CHECK(!error.empty());
    std::cout << "serialize round trip OK (" << json.size() << " bytes)\n";
}

}

int main(int argc, char**) {
    if (argc > 1) {
        
        Registry::EnsureRegistered();
        BlueprintQuest quest = build_fetch_quest();
        std::cout << Compile(quest).quest_lua;
        return 0;
    }
    test_registry_integrity();
    test_instantiate();
    test_link_rules();
    test_remove_node();
    test_dynamic_sequence_pins();
    test_exec_in_degree();
    test_compile_fetch_quest();
    test_compile_branch_and_reconvergence();
    test_compile_entity_event_thread();
    test_compile_static_prop_hold_prompt();
    test_compile_validation_errors();
    test_serialize_round_trip();
    std::cout << "all blueprint tests passed\n";
    return 0;
}
