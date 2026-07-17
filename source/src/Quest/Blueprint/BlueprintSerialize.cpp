#include "BlueprintSerialize.h"

#include "crude_json.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace Quest {
namespace Bp {

namespace {

using crude_json::value;

value world_to_json(const Quest::WorldReference& w) {
    value v;
    v["level_path"] = w.level_path;
    v["level_id"] = w.level_id;
    v["entity_name"] = w.entity_name;
    v["entity_hash"] = (double)w.entity_hash;
    v["x"] = (double)w.x;
    v["y"] = (double)w.y;
    v["z"] = (double)w.z;
    v["authored"] = w.authored_instance;
    value hashes;
    for (size_t i = 0; i < w.model_hashes.size(); ++i) {
        hashes[i] = (double)w.model_hashes[i];
    }
    v["model_hashes"] = hashes.is_array() ? hashes : value(crude_json::type_t::array);
    return v;
}

std::string json_str(const value& v, const char* key) {
    return v.contains(key) && v[key].is_string()
               ? v[key].get<crude_json::string>() : std::string();
}

double json_num(const value& v, const char* key) {
    return v.contains(key) && v[key].is_number()
               ? v[key].get<crude_json::number>() : 0.0;
}

bool json_bool(const value& v, const char* key) {
    return v.contains(key) && v[key].is_boolean()
               ? v[key].get<crude_json::boolean>() : false;
}

Quest::WorldReference world_from_json(const value& v) {
    Quest::WorldReference w;
    w.level_path = json_str(v, "level_path");
    w.level_id = json_str(v, "level_id");
    w.entity_name = json_str(v, "entity_name");
    w.entity_hash = (uint32_t)json_num(v, "entity_hash");
    w.x = (float)json_num(v, "x");
    w.y = (float)json_num(v, "y");
    w.z = (float)json_num(v, "z");
    w.authored_instance = json_bool(v, "authored");
    if (v.contains("model_hashes") && v["model_hashes"].is_array()) {
        for (const value& h : v["model_hashes"].get<crude_json::array>()) {
            if (h.is_number()) {
                w.model_hashes.push_back(
                    (uint32_t)h.get<crude_json::number>());
            }
        }
    }
    return w;
}

value prerequisite_to_json(const Quest::AuthoredNode& p) {
    value v;
    v["id"] = (double)p.id;
    v["kind"] = (double)(int)p.kind;
    v["story_start"] = p.story_start;
    v["story_end"] = p.story_end;
    v["other_quest"] = p.other_quest;
    v["quest_state"] = (double)(int)p.quest_state;
    v["gameflow_flag"] = p.gameflow_flag;
    v["lua_condition"] = p.lua_condition;
    v["hero_requirement"] = (double)(int)p.hero_requirement;
    v["numeric_comparison"] = (double)(int)p.numeric_comparison;
    v["hero_value"] = p.hero_value;
    v["hero_option"] = p.hero_option;
    v["expected"] = p.expected;
    return v;
}

Quest::AuthoredNode prerequisite_from_json(const value& v) {
    Quest::AuthoredNode p;
    p.id = (int)json_num(v, "id");
    p.kind = (Quest::AuthoredNodeKind)(int)json_num(v, "kind");
    p.story_start = json_str(v, "story_start");
    p.story_end = json_str(v, "story_end");
    p.other_quest = json_str(v, "other_quest");
    p.quest_state = (Quest::RequiredQuestState)(int)json_num(v, "quest_state");
    p.gameflow_flag = json_str(v, "gameflow_flag");
    p.lua_condition = json_str(v, "lua_condition");
    p.hero_requirement =
        (Quest::HeroRequirementKind)(int)json_num(v, "hero_requirement");
    p.numeric_comparison =
        (Quest::NumericComparison)(int)json_num(v, "numeric_comparison");
    p.hero_value = json_str(v, "hero_value");
    p.hero_option = json_str(v, "hero_option");
    p.expected = json_bool(v, "expected");
    return p;
}

}

std::string SerializeToString(const BlueprintQuest& quest) {
    value root;
    root["version"] = 1.0;
    root["quest_id"] = quest.quest_id;
    root["quest_title"] = quest.quest_title;
    root["next_id"] = (double)quest.next_id;
    root["next_prerequisite_id"] = (double)quest.next_prerequisite_id;

    value vars(crude_json::type_t::array);
    for (size_t i = 0; i < quest.variables.size(); ++i) {
        const Variable& var = quest.variables[i];
        value v;
        v["name"] = var.name;
        v["type"] = (double)(int)var.type;
        v["b"] = var.def.b;
        v["num"] = var.def.num;
        v["str"] = var.def.str;
        vars[i] = v;
    }
    root["variables"] = vars;

    value nodes(crude_json::type_t::array);
    for (size_t i = 0; i < quest.nodes.size(); ++i) {
        const Node& node = quest.nodes[i];
        value n;
        n["id"] = (double)node.id;
        n["type"] = node.type;
        n["x"] = (double)node.x;
        n["y"] = (double)node.y;
        n["comment"] = node.comment;
        n["prop"] = node.prop;
        if (node.type.rfind("prereq.", 0) == 0) {
            n["prereq"] = prerequisite_to_json(node.prereq);
        }
        value pins(crude_json::type_t::array);
        for (size_t j = 0; j < node.pins.size(); ++j) {
            const Pin& pin = node.pins[j];
            value p;
            p["id"] = (double)pin.id;
            p["name"] = pin.name;
            p["type"] = (double)(int)pin.type;
            p["dir"] = (double)(int)pin.dir;
            p["optional"] = pin.optional;
            p["dynamic"] = pin.dynamic;
            p["b"] = pin.value.b;
            p["num"] = pin.value.num;
            p["str"] = pin.value.str;
            value vec(crude_json::type_t::array);
            vec[0] = (double)pin.value.vec[0];
            vec[1] = (double)pin.value.vec[1];
            vec[2] = (double)pin.value.vec[2];
            p["vec"] = vec;
            p["world"] = world_to_json(pin.value.world);
            value item;
            item["record_hash"] = (double)pin.value.item.record_hash;
            item["internal_name"] = pin.value.item.internal_name;
            item["display_name"] = pin.value.item.display_name;
            item["model_path"] = pin.value.item.model_path;
            p["item"] = item;
            pins[j] = p;
        }
        n["pins"] = pins;
        nodes[i] = n;
    }
    root["nodes"] = nodes;

    value links(crude_json::type_t::array);
    for (size_t i = 0; i < quest.links.size(); ++i) {
        value l;
        l["id"] = (double)quest.links[i].id;
        l["from"] = (double)quest.links[i].from_pin;
        l["to"] = (double)quest.links[i].to_pin;
        links[i] = l;
    }
    root["links"] = links;

    value prereqs(crude_json::type_t::array);
    for (size_t i = 0; i < quest.prerequisites.size(); ++i) {
        prereqs[i] = prerequisite_to_json(quest.prerequisites[i]);
    }
    root["prerequisites"] = prereqs;

    return root.dump(2);
}

bool DeserializeFromString(const std::string& text, BlueprintQuest& out,
                           std::string& error) {
    const value root = value::parse(text);
    if (!root.is_object()) {
        error = "Not a valid .bpquest.json file.";
        return false;
    }
    if ((int)json_num(root, "version") != 1) {
        error = "Unsupported blueprint quest version.";
        return false;
    }
    out = BlueprintQuest{};
    out.quest_id = json_str(root, "quest_id");
    out.quest_title = json_str(root, "quest_title");
    out.next_id = (int)json_num(root, "next_id");
    out.next_prerequisite_id =
        (int)json_num(root, "next_prerequisite_id");
    if (out.quest_id.empty()) {
        error = "Missing quest_id.";
        return false;
    }

    if (root.contains("variables") && root["variables"].is_array()) {
        for (const value& v : root["variables"].get<crude_json::array>()) {
            Variable var;
            var.name = json_str(v, "name");
            var.type = (PinType)(int)json_num(v, "type");
            var.def.b = json_bool(v, "b");
            var.def.num = json_num(v, "num");
            var.def.str = json_str(v, "str");
            out.variables.push_back(std::move(var));
        }
    }

    if (root.contains("nodes") && root["nodes"].is_array()) {
        for (const value& n : root["nodes"].get<crude_json::array>()) {
            Node node;
            node.id = (int)json_num(n, "id");
            node.type = json_str(n, "type");
            node.x = (float)json_num(n, "x");
            node.y = (float)json_num(n, "y");
            node.comment = json_str(n, "comment");
            node.prop = json_str(n, "prop");
            if (n.contains("prereq")) {
                node.prereq = prerequisite_from_json(n["prereq"]);
            }
            if (n.contains("pins") && n["pins"].is_array()) {
                for (const value& p : n["pins"].get<crude_json::array>()) {
                    Pin pin;
                    pin.id = (int)json_num(p, "id");
                    pin.name = json_str(p, "name");
                    pin.type = (PinType)(int)json_num(p, "type");
                    pin.dir = (PinDir)(int)json_num(p, "dir");
                    pin.optional = json_bool(p, "optional");
                    pin.dynamic = json_bool(p, "dynamic");
                    pin.value.b = json_bool(p, "b");
                    pin.value.num = json_num(p, "num");
                    pin.value.str = json_str(p, "str");
                    if (p.contains("vec") && p["vec"].is_array()) {
                        const auto& vec = p["vec"].get<crude_json::array>();
                        for (size_t k = 0; k < 3 && k < vec.size(); ++k) {
                            if (vec[k].is_number()) {
                                pin.value.vec[k] = (float)
                                    vec[k].get<crude_json::number>();
                            }
                        }
                    }
                    if (p.contains("world")) {
                        pin.value.world = world_from_json(p["world"]);
                    }
                    if (p.contains("item")) {
                        const value& item = p["item"];
                        pin.value.item.record_hash =
                            (uint32_t)json_num(item, "record_hash");
                        pin.value.item.internal_name =
                            json_str(item, "internal_name");
                        pin.value.item.display_name =
                            json_str(item, "display_name");
                        pin.value.item.model_path =
                            json_str(item, "model_path");
                    }
                    node.pins.push_back(std::move(pin));
                }
            }
            out.nodes.push_back(std::move(node));
        }
    }

    if (root.contains("links") && root["links"].is_array()) {
        for (const value& l : root["links"].get<crude_json::array>()) {
            Link link;
            link.id = (int)json_num(l, "id");
            link.from_pin = (int)json_num(l, "from");
            link.to_pin = (int)json_num(l, "to");
            out.links.push_back(link);
        }
    }

    if (root.contains("prerequisites") &&
        root["prerequisites"].is_array()) {
        for (const value& p :
             root["prerequisites"].get<crude_json::array>()) {
            out.prerequisites.push_back(prerequisite_from_json(p));
        }
    }
    return true;
}

bool SaveToFile(const BlueprintQuest& quest, const std::string& path,
                std::string& error) {
    std::error_code ec;
    std::filesystem::create_directories(
        std::filesystem::path(path).parent_path(), ec);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        error = "Cannot write " + path;
        return false;
    }
    const std::string text = SerializeToString(quest);
    f.write(text.data(), (std::streamsize)text.size());
    if (!f) {
        error = "Write failed for " + path;
        return false;
    }
    return true;
}

bool LoadFromFile(const std::string& path, BlueprintQuest& out,
                  std::string& error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        error = "Cannot read " + path;
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return DeserializeFromString(ss.str(), out, error);
}

}
}
