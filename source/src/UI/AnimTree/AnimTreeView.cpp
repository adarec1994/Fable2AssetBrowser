#include "AnimTreeView.h"

#include "GDB/GdbParser.h"
#include "Level/Core/LevelLoader.h"
#include "UI/OutputLog.h"
#include "UI/Quest/Blueprint/vendor/widgets.h"
#include "Utilities/State.h"
#include "animations/AnimBank.h"

#include "imgui.h"
#include "imgui_stdlib.h"
#include <imgui_node_editor.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace ed = ax::NodeEditor;

namespace AnimTreeUI {

namespace {

enum class Category : int {
    Locomotion = 0,
    Combat,
    HitReaction,
    Expression,
    Action,
    Misc,
    Count,
};

const char* kCategoryNames[] = {
    "Locomotion", "Combat", "Hits & Death", "Expressions", "Actions",
    "Misc",
};

ImU32 category_color(Category cat, bool transition)
{
    if (transition) return IM_COL32(96, 108, 122, 235);
    switch (cat) {
        case Category::Locomotion:  return IM_COL32(52, 110, 205, 235);
        case Category::Combat:      return IM_COL32(182, 62, 51, 235);
        case Category::HitReaction: return IM_COL32(132, 66, 168, 235);
        case Category::Expression:  return IM_COL32(46, 152, 92, 235);
        case Category::Action:      return IM_COL32(198, 134, 24, 235);
        default:                    return IM_COL32(92, 104, 118, 235);
    }
}

struct Row {
    std::string label;
    std::string clip;
    uint32_t key = 0;
    float duration = -1.0f;
    int slot_index = -1;
    int chain_depth = 0;
};

struct VNode {
    int id = 0;
    bool is_transition = false;
    bool is_default = false;
    bool is_any_state = false;
    bool overrides_base = false;
    Category cat = Category::Misc;
    std::string title;
    std::string subtitle;
    std::vector<Row> rows;
    float x = 0.0f;
    float y = 0.0f;
    bool has_edges = false;
};

enum EdgeKind {
    kEdgeAuthored = 0,
    kEdgeFlow,
    kEdgeEvent,
    kEdgeOutcome,
    kEdgeReturn,
};

struct VLink {
    int id = 0;
    int from_node = 0;
    int to_node = 0;
    int kind = kEdgeAuthored;
    std::string label;
};

struct ViewState {
    bool open = false;
    bool bring_front = false;
    uint32_t entity = 0;
    std::string title;
    Gdb::EntityAnimTree tree;
    std::vector<VNode> nodes;
    std::vector<VLink> links;
    ed::EditorContext* ctx = nullptr;
    bool layout_pending = true;
    bool fit_pending = true;
    int selected = 0;
    std::string filter;
    bool cat_visible[(int)Category::Count] = {true, true, true, true, true,
                                              true};
    bool show_transitions = true;
    bool show_conditions = true;
    bool show_returns = false;
    bool all_labels = false;
};

ViewState g;

bool starts_with(const std::string& s, const char* prefix)
{
    const size_t n = std::strlen(prefix);
    return s.size() >= n && s.compare(0, n, prefix) == 0;
}

bool ends_with(const std::string& s, const char* suffix, size_t* cut)
{
    const size_t n = std::strlen(suffix);
    if (s.size() <= n || s.compare(s.size() - n, n, suffix) != 0) {
        return false;
    }
    if (cut) *cut = s.size() - n;
    return true;
}

std::string humanize(const std::string& name)
{
    std::string out;
    out.reserve(name.size() + 8);
    for (size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        if (c == '_') {
            out.push_back(' ');
            continue;
        }
        if (i > 0 && std::isupper((unsigned char)c) &&
            (std::islower((unsigned char)name[i - 1]) ||
             (i + 1 < name.size() &&
              std::islower((unsigned char)name[i + 1]) &&
              std::isupper((unsigned char)name[i - 1])))) {
            out.push_back(' ');
        }
        out.push_back(c);
    }
    return out;
}

std::string lowercase(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return (char)std::tolower(c);
    });
    return s;
}

bool contains_any(const std::string& base, const char* const* words,
                  size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        if (base.find(words[i]) != std::string::npos) return true;
    }
    return false;
}

Category categorize(const std::string& base, bool triad)
{
    static const char* hit[] = {
        "GetHit", "Knockdown", "KnockedBack", "Knockback", "Stunned",
        "Die", "Death", "Electrified", "Shove", "Response", "Recoil",
        "Stagger", "Recover", "Damocles", "Facehugged",
    };
    static const char* combat[] = {
        "Strike", "Attack", "Block", "Parry", "Charge", "Cast",
        "Flourish", "Shot", "Aim", "Reload", "Combat", "Weapon",
        "Sword", "Gun", "Rifle", "Crossbow", "Stab", "Execution",
        "Kill", "Rise", "Curse", "AoE", "Spell", "Will",
    };
    static const char* loco[] = {
        "Idle", "Walk", "Run", "Strafe", "Turn", "Pose", "Fly",
        "Hover", "Swim", "Sprint", "Move", "Underground",
    };
    static const char* expr[] = {
        "React", "Dance", "Cheer", "Jeer", "Laugh", "Clap", "Wave",
        "Bow", "Curtsey", "Insult", "Flirt", "Apolog", "ArmPump",
        "Thumbs", "Point", "Beckon", "Scared", "Bored",
    };
    if (contains_any(base, hit, sizeof(hit) / sizeof(hit[0]))) {
        return Category::HitReaction;
    }
    if (contains_any(base, combat, sizeof(combat) / sizeof(combat[0]))) {
        return Category::Combat;
    }
    if (contains_any(base, loco, sizeof(loco) / sizeof(loco[0]))) {
        return Category::Locomotion;
    }
    if (contains_any(base, expr, sizeof(expr) / sizeof(expr[0]))) {
        return Category::Expression;
    }
    return triad ? Category::Action : Category::Misc;
}

enum Role {
    kRoleSingle = 0,
    kRoleInto,
    kRoleLoop,
    kRoleOut,
    kRoleOutSuccess,
    kRoleOutFailure,
    kRoleOutUpperBody,
};

const char* role_label(int role)
{
    switch (role) {
        case kRoleInto:         return "Into";
        case kRoleLoop:         return "Loop";
        case kRoleOut:          return "Out";
        case kRoleOutSuccess:   return "Out (success)";
        case kRoleOutFailure:   return "Out (failure)";
        case kRoleOutUpperBody: return "Out (upper body)";
        default:                return "Clip";
    }
}

struct ClipInfo {
    std::string name;
    float duration = -1.0f;
};

std::unordered_map<uint32_t, ClipInfo> build_clip_lookup()
{
    std::unordered_map<uint32_t, ClipInfo> out;
    out.reserve(S.anim_clips.size() * 2 + 1);
    for (const Anim::AnimClip& clip : S.anim_clips) {
        ClipInfo info;
        info.name = clip.name;
        info.duration = Anim::clip_duration_seconds(clip);
        out.emplace(clip.key0, std::move(info));
    }
    return out;
}

struct StateBuild {
    std::string base;
    Category cat = Category::Misc;
    bool triad = false;
    std::vector<Row> rows;
    bool overrides_base = false;
    int node_id = 0;
};

// imgui_node_editor requires ids to be unique across nodes, pins and
// links, not merely within each kind.
int pin_in(int node_id) { return 1000000 + node_id * 2; }
int pin_out(int node_id) { return 1000000 + node_id * 2 + 1; }
constexpr int kFirstLinkId = 4000000;

void build_graph()
{
    g.nodes.clear();
    g.links.clear();
    g.selected = 0;

    const auto clips = build_clip_lookup();
    auto clip_name = [&](uint32_t key) -> std::string {
        const auto it = clips.find(key);
        if (it != clips.end() && !it->second.name.empty()) {
            return it->second.name;
        }
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%08X", key);
        return std::string(buf);
    };
    auto clip_duration = [&](uint32_t key) -> float {
        const auto it = clips.find(key);
        return it != clips.end() ? it->second.duration : -1.0f;
    };
    auto make_row = [&](const char* label, uint32_t key, int slot_index,
                        int depth) {
        Row row;
        row.label = label;
        row.key = key;
        row.clip = clip_name(key);
        row.duration = clip_duration(key);
        row.slot_index = slot_index;
        row.chain_depth = depth;
        return row;
    };

    const bool multi_chain = g.tree.chain.size() > 1;

    // Pass 1: parse slot names into states / transitions.
    struct TransitionBuild {
        std::string label;
        std::string from;
        std::string to;
        Category cat = Category::Locomotion;
        std::vector<Row> rows;
        int node_id = 0;
    };
    std::map<std::string, StateBuild> states;
    std::vector<TransitionBuild> transitions;

    auto state_for = [&](const std::string& base) -> StateBuild& {
        StateBuild& state = states[base];
        if (state.base.empty()) state.base = base;
        return state;
    };

    const std::vector<Gdb::AnimTreeSlot>& slots = g.tree.slots;
    for (size_t i = 0; i < slots.size(); ++i) {
        const Gdb::AnimTreeSlot& slot = slots[i];
        const std::string& name = slot.slot_name;
        if (name.empty()) continue;

        // Directional transition clips, e.g.
        // CombatStrafeTransitionLeftIntoBackwards / ...RightOpenLFToForwards.
        const size_t tr = name.find("Transition");
        if (tr != std::string::npos && tr > 0 &&
            tr + 10 < name.size()) {
            const std::string family = name.substr(0, tr);
            std::string rest = name.substr(tr + 10);
            std::string from;
            std::string to;
            size_t split = rest.find("Into");
            size_t skip = 4;
            if (split == std::string::npos) {
                split = rest.find("To");
                skip = 2;
            }
            if (split != std::string::npos) {
                from = rest.substr(0, split);
                to = rest.substr(split + skip);
            }
            auto strip_foot = [](std::string dir) {
                for (const char* f : {"OpenLF", "CrossedLF", "OpenRF",
                                      "CrossedRF", "LF", "RF"}) {
                    size_t cut = 0;
                    const size_t n = std::strlen(f);
                    if (dir.size() > n &&
                        dir.compare(dir.size() - n, n, f) == 0) {
                        cut = dir.size() - n;
                        dir.erase(cut);
                        break;
                    }
                }
                return dir;
            };
            TransitionBuild t;
            t.label = name;
            t.cat = categorize(family.empty() ? name : family, false);
            if (!from.empty() && !to.empty()) {
                t.from = family + strip_foot(from);
                t.to = family + strip_foot(to);
            }
            t.rows.push_back(make_row("Clip", slot.clip_key, (int)i,
                                      slot.chain_depth));
            for (uint32_t var : slot.variation_keys) {
                t.rows.push_back(make_row("Var", var, (int)i,
                                          slot.chain_depth));
            }
            transitions.push_back(std::move(t));
            continue;
        }

        // Entry transitions: IntoWalk etc.
        if (starts_with(name, "Into") && name.size() > 4) {
            TransitionBuild t;
            t.label = name;
            t.to = name.substr(4);
            t.cat = categorize(t.to, false);
            t.rows.push_back(make_row("Clip", slot.clip_key, (int)i,
                                      slot.chain_depth));
            transitions.push_back(std::move(t));
            continue;
        }

        int role = kRoleSingle;
        std::string base = name;
        size_t cut = 0;
        if (ends_with(name, "OutOfSuccess", &cut)) {
            role = kRoleOutSuccess;
        } else if (ends_with(name, "OutOfFailure", &cut)) {
            role = kRoleOutFailure;
        } else if (ends_with(name, "OutOfUpperBody", &cut)) {
            role = kRoleOutUpperBody;
        } else if (ends_with(name, "OutOf", &cut) ||
                   ends_with(name, "Outof", &cut)) {
            role = kRoleOut;
        } else if (ends_with(name, "Into", &cut)) {
            role = kRoleInto;
        } else if (ends_with(name, "Loop", &cut)) {
            role = kRoleLoop;
        } else if (ends_with(name, "Cycle", &cut)) {
            role = kRoleLoop;
        }
        if (role != kRoleSingle && cut > 0) base = name.substr(0, cut);

        StateBuild& state = state_for(base);
        state.rows.push_back(
            make_row(role == kRoleSingle ? "Clip" : role_label(role),
                     slot.clip_key, (int)i, slot.chain_depth));
        for (uint32_t var : slot.variation_keys) {
            state.rows.push_back(
                make_row("Var", var, (int)i, slot.chain_depth));
        }
        if (role == kRoleInto || role == kRoleLoop || role >= kRoleOut) {
            state.triad = true;
        }
        if (slot.overridden_deeper && multi_chain) {
            state.overrides_base = true;
        }
    }

    // Default / entry state.
    std::string default_state;
    for (const char* candidate : {"Idle", "CombatIdle", "Pose", "Walk"}) {
        if (states.count(candidate)) {
            default_state = candidate;
            break;
        }
    }

    // Ensure transition endpoints exist as states before node creation.
    for (TransitionBuild& t : transitions) {
        if (!t.to.empty() && !states.count(t.to)) {
            if (t.from.empty()) {
                // IntoX with no X slot: keep the clip on a bare state.
                state_for(t.to);
            } else {
                t.from.clear();
                t.to.clear();
            }
        }
        if (!t.from.empty() && !states.count(t.from)) {
            t.from.clear();
            t.to.clear();
        }
    }

    int next_id = 1;
    int next_link = kFirstLinkId;

    for (auto& [base, state] : states) {
        state.cat = categorize(base, state.triad);
        state.node_id = next_id++;
        VNode node;
        node.id = state.node_id;
        node.cat = state.cat;
        node.title = humanize(base);
        node.rows = state.rows;
        node.overrides_base = state.overrides_base;
        node.is_default = base == default_state;
        if (node.is_default) node.subtitle = "default state";
        g.nodes.push_back(std::move(node));
    }
    auto state_node_id = [&](const std::string& base) -> int {
        const auto it = states.find(base);
        return it == states.end() ? 0 : it->second.node_id;
    };
    auto node_by_id = [&](int id) -> VNode* {
        for (VNode& node : g.nodes) {
            if (node.id == id) return &node;
        }
        return nullptr;
    };

    const int default_id = state_node_id(default_state);
    for (TransitionBuild& t : transitions) {
        t.node_id = next_id++;
        VNode node;
        node.id = t.node_id;
        node.is_transition = true;
        node.cat = t.cat;
        node.title = humanize(t.label);
        node.rows = t.rows;
        g.nodes.push_back(std::move(node));

        int from_id = t.from.empty() ? default_id : state_node_id(t.from);
        const int to_id = t.to.empty() ? 0 : state_node_id(t.to);
        if (from_id != 0 && to_id != 0 && from_id != to_id) {
            g.links.push_back({next_link++, from_id, t.node_id});
            g.links.push_back({next_link++, t.node_id, to_id});
            node_by_id(from_id)->has_edges = true;
            node_by_id(to_id)->has_edges = true;
            node_by_id(t.node_id)->has_edges = true;
        }
    }

    // Turn-in-place clips flow out of and back into the default state.
    if (default_id != 0) {
        for (VNode& node : g.nodes) {
            if (node.is_transition || node.id == default_id) continue;
            if (!starts_with(node.title, "Turn ") &&
                node.title != "Turn") {
                continue;
            }
            g.links.push_back({next_link++, default_id, node.id,
                               kEdgeFlow, "turn"});
            g.links.push_back({next_link++, node.id, default_id,
                               kEdgeReturn, ""});
            node.has_edges = true;
            if (VNode* def = node_by_id(default_id)) {
                def->has_edges = true;
            }
        }
    }

    // Condition edges derived from the engine's known slot semantics.
    {
        std::set<std::tuple<int, int, std::string>> edge_seen;
        auto mark = [&](int node_id) {
            if (VNode* node = node_by_id(node_id)) node->has_edges = true;
        };
        auto add_edge = [&](int from, int to, const std::string& label,
                            int kind) {
            if (from == 0 || to == 0 || from == to) return;
            if (!edge_seen.insert({from, to, label}).second) return;
            g.links.push_back({next_link++, from, to, kind, label});
            mark(from);
            mark(to);
        };
        std::set<std::string> authored_targets;
        for (const TransitionBuild& t : transitions) {
            if (!t.to.empty()) authored_targets.insert(t.to);
        }

        struct DirWord { const char* suffix; const char* word; };
        static const DirWord kDirWords[] = {
            {"BackRight", "back-right"}, {"BackLeft", "back-left"},
            {"Backwards", "backwards"}, {"Forwards", "forwards"},
            {"Backward", "backwards"}, {"Forward", "forwards"},
            {"Back", "behind"}, {"Front", "front"},
            {"Left", "left"}, {"Right", "right"},
        };
        auto dir_word = [&](const std::string& base,
                            const std::string& prefix) -> const char* {
            if (base.size() < prefix.size() ||
                base.compare(0, prefix.size(), prefix) != 0) {
                return nullptr;
            }
            const std::string rest = base.substr(prefix.size());
            if (rest.empty()) return "";
            for (const DirWord& d : kDirWords) {
                if (rest == d.suffix) return d.word;
            }
            return nullptr;
        };

        const int idle_id = state_node_id("Idle");
        const int walk_id = state_node_id("Walk");
        const int run_id = state_node_id("Run");
        const int combat_idle_id = state_node_id("CombatIdle");
        const int block_id = state_node_id("BlockPose");
        const int action_src = combat_idle_id ? combat_idle_id
                                              : default_id;

        // Locomotion flow.
        if (!authored_targets.count("Walk")) {
            add_edge(idle_id, walk_id, "move", kEdgeFlow);
        }
        add_edge(walk_id, idle_id, "stop", kEdgeFlow);
        add_edge(walk_id, run_id, "speed up", kEdgeFlow);
        add_edge(run_id, walk_id, "slow down", kEdgeFlow);
        add_edge(idle_id, combat_idle_id, "combat starts", kEdgeFlow);
        add_edge(combat_idle_id, idle_id, "combat ends", kEdgeFlow);

        int any_state_id = 0;
        auto any_state = [&]() {
            if (any_state_id != 0) return any_state_id;
            any_state_id = next_id++;
            VNode node;
            node.id = any_state_id;
            node.is_any_state = true;
            node.cat = Category::HitReaction;
            node.title = "Any State";
            node.subtitle = "engine events";
            node.has_edges = true;
            g.nodes.push_back(std::move(node));
            return any_state_id;
        };

        std::vector<std::string> recover_states;
        for (const auto& [base, state] : states) {
            if (starts_with(base, "KnockdownRecover")) {
                recover_states.push_back(base);
            }
        }

        for (const auto& [base, state] : states) {
            const int sid = state.node_id;
            if (sid == 0 || sid == default_id) continue;

            if (const char* dir = dir_word(base, "CombatStrafe")) {
                if (*dir) {
                    add_edge(combat_idle_id ? combat_idle_id : idle_id,
                             sid, std::string("strafe ") + dir,
                             kEdgeFlow);
                    add_edge(sid,
                             combat_idle_id ? combat_idle_id : idle_id,
                             "stop", kEdgeReturn);
                }
                continue;
            }
            if (const char* dir = dir_word(base, "GetHit")) {
                add_edge(any_state(), sid,
                         *dir ? std::string("hit from ") + dir
                              : std::string("hit"),
                         kEdgeEvent);
                add_edge(sid, default_id, "recovers", kEdgeReturn);
                continue;
            }
            if (const char* dir = dir_word(base, "Shove")) {
                add_edge(any_state(), sid,
                         *dir ? std::string("shoved ") + dir
                              : std::string("shoved"),
                         kEdgeEvent);
                add_edge(sid, default_id, "recovers", kEdgeReturn);
                continue;
            }
            if (const char* dir = dir_word(base, "ResponseToKnockdown")) {
                add_edge(any_state(), sid,
                         *dir ? std::string("knocked down from ") + dir
                              : std::string("knocked down"),
                         kEdgeEvent);
                for (const std::string& rec : recover_states) {
                    add_edge(sid, state_node_id(rec), "get up",
                             kEdgeFlow);
                }
                continue;
            }
            if (starts_with(base, "KnockdownRecover")) {
                add_edge(sid, default_id, "recovered", kEdgeReturn);
                continue;
            }
            if (base == "Die" || base == "Death") {
                add_edge(any_state(), sid, "health reaches 0",
                         kEdgeEvent);
                continue;
            }
            if (base == "Electrified") {
                add_edge(any_state(), sid, "electrocuted", kEdgeEvent);
                add_edge(sid, default_id, "recovers", kEdgeReturn);
                continue;
            }
            if (starts_with(base, "Stunned")) {
                add_edge(any_state(), sid, "stunned", kEdgeEvent);
                add_edge(sid, default_id, "recovers", kEdgeReturn);
                continue;
            }
            if (base.find("StrikeRecoil") != std::string::npos) {
                add_edge(any_state(), sid, "attack deflected",
                         kEdgeEvent);
                add_edge(sid, action_src, "recovers", kEdgeReturn);
                continue;
            }
            if (base == "BlockPose") {
                add_edge(action_src, sid, "block held", kEdgeFlow);
                add_edge(sid, action_src, "block released",
                         kEdgeReturn);
                continue;
            }
            if (starts_with(base, "BlockParry")) {
                add_edge(block_id ? block_id : action_src, sid, "parry",
                         kEdgeFlow);
                add_edge(sid, action_src, "", kEdgeReturn);
                continue;
            }
            if (ends_with(base, "FollowUp", nullptr)) {
                const std::string root =
                    base.substr(0, base.size() - 8);
                const int root_id = state_node_id(root);
                if (root_id != 0) {
                    add_edge(root_id, sid, "combo", kEdgeFlow);
                    add_edge(sid, action_src, "", kEdgeReturn);
                    continue;
                }
            }
            if (starts_with(base, "RunAttack")) {
                add_edge(run_id ? run_id : action_src, sid,
                         "attack while running", kEdgeFlow);
                add_edge(sid, action_src, "", kEdgeReturn);
                continue;
            }
            if (starts_with(base, "RiseAttack") ||
                starts_with(base, "RiseSpinAttack")) {
                const int rise_id = state_node_id("Rise");
                add_edge(rise_id ? rise_id : action_src, sid,
                         "attack while rising", kEdgeFlow);
                add_edge(sid, action_src, "", kEdgeReturn);
                continue;
            }
            if (!state.triad && state.cat == Category::Combat &&
                (base.find("Strike") != std::string::npos ||
                 base.find("Attack") != std::string::npos) &&
                base.find("Recoil") == std::string::npos) {
                const char* dir = dir_word(base, "Strike");
                std::string label = "attack";
                if (dir && *dir) label = std::string("attack ") + dir;
                add_edge(action_src, sid, label, kEdgeFlow);
                add_edge(sid, action_src, "", kEdgeReturn);
                continue;
            }

            if (state.triad) {
                if (!authored_targets.count(base)) {
                    std::string label;
                    switch (state.cat) {
                        case Category::Expression:
                            label = "expression triggered";
                            break;
                        case Category::Combat:
                            label = "cast / use";
                            break;
                        case Category::Action:
                            label = "interaction starts";
                            break;
                        default:
                            label = "starts";
                            break;
                    }
                    add_edge(default_id, sid, label, kEdgeFlow);
                }
                bool success = false;
                bool failure = false;
                bool upper = false;
                bool plain_out = false;
                for (const Row& row : state.rows) {
                    if (row.label == role_label(kRoleOutSuccess)) {
                        success = true;
                    } else if (row.label == role_label(kRoleOutFailure)) {
                        failure = true;
                    } else if (row.label ==
                               role_label(kRoleOutUpperBody)) {
                        upper = true;
                    } else if (row.label == role_label(kRoleOut)) {
                        plain_out = true;
                    }
                }
                if (success) {
                    add_edge(sid, default_id, "on success", kEdgeOutcome);
                }
                if (failure) {
                    add_edge(sid, default_id, "on failure", kEdgeOutcome);
                }
                if (upper || plain_out || (!success && !failure)) {
                    add_edge(sid, default_id, "finished", kEdgeReturn);
                }
            }
        }
    }

    // Layout: one column band per category, wrapping long columns.
    const float kColumnWidth = 430.0f;
    const float kColumnGap = 60.0f;
    const float kMaxColumnHeight = 2500.0f;
    float band_x = 0.0f;
    for (int cat = 0; cat < (int)Category::Count; ++cat) {
        std::vector<VNode*> members;
        for (VNode& node : g.nodes) {
            if ((int)node.cat == cat && !node.is_transition) {
                members.push_back(&node);
            }
        }
        std::vector<VNode*> band_transitions;
        for (VNode& node : g.nodes) {
            if ((int)node.cat == cat && node.is_transition &&
                !node.has_edges) {
                band_transitions.push_back(&node);
            }
        }
        members.insert(members.end(), band_transitions.begin(),
                       band_transitions.end());
        if (members.empty()) continue;
        std::stable_sort(members.begin(), members.end(),
                         [](const VNode* a, const VNode* b) {
            if (a->is_default != b->is_default) return a->is_default;
            if (a->has_edges != b->has_edges) return a->has_edges;
            if (a->is_transition != b->is_transition) {
                return !a->is_transition;
            }
            return a->title < b->title;
        });
        float x = band_x;
        float y = 0.0f;
        float max_x = band_x;
        for (VNode* node : members) {
            const size_t shown_rows = std::min<size_t>(node->rows.size(), 8);
            const float est_h =
                74.0f + (float)shown_rows * 23.0f +
                (node->rows.size() > shown_rows ? 20.0f : 0.0f);
            if (y > 0.0f && y + est_h > kMaxColumnHeight) {
                x += kColumnWidth;
                y = 0.0f;
            }
            node->x = x;
            node->y = y;
            y += est_h + 26.0f;
            max_x = std::max(max_x, x);
        }
        band_x = max_x + kColumnWidth + kColumnGap;
    }
    // Connected transition nodes sit between their endpoints.
    for (VNode& node : g.nodes) {
        if (!node.is_transition || !node.has_edges) continue;
        const VLink* in_link = nullptr;
        const VLink* out_link = nullptr;
        for (const VLink& link : g.links) {
            if (link.to_node == node.id) in_link = &link;
            if (link.from_node == node.id) out_link = &link;
        }
        if (!in_link || !out_link) continue;
        const VNode* from = nullptr;
        const VNode* to = nullptr;
        for (const VNode& other : g.nodes) {
            if (other.id == in_link->from_node) from = &other;
            if (other.id == out_link->to_node) to = &other;
        }
        if (!from || !to) continue;
        node.x = (from->x + to->x) * 0.5f + 60.0f;
        node.y = (from->y + to->y) * 0.5f + 40.0f;
    }
    // Resolve transition-node overlaps with a simple vertical shake-out.
    {
        std::vector<VNode*> connected;
        for (VNode& node : g.nodes) {
            if (node.is_transition && node.has_edges) {
                connected.push_back(&node);
            }
        }
        std::stable_sort(connected.begin(), connected.end(),
                         [](const VNode* a, const VNode* b) {
            if (a->x != b->x) return a->x < b->x;
            return a->y < b->y;
        });
        for (size_t i = 1; i < connected.size(); ++i) {
            VNode* prev = connected[i - 1];
            VNode* cur = connected[i];
            if (std::abs(cur->x - prev->x) < 200.0f &&
                cur->y < prev->y + 96.0f) {
                cur->y = prev->y + 96.0f;
            }
        }
    }

    g.layout_pending = true;
    g.fit_pending = true;
}

bool node_matches_filter(const VNode& node, const std::string& filter)
{
    if (filter.empty()) return true;
    if (lowercase(node.title).find(filter) != std::string::npos) {
        return true;
    }
    for (const Row& row : node.rows) {
        if (lowercase(row.clip).find(filter) != std::string::npos) {
            return true;
        }
    }
    return false;
}

struct EdgeStyle {
    ImVec4 color;
    float thickness;
};

EdgeStyle edge_style(int kind)
{
    switch (kind) {
        case kEdgeFlow:
            return {ImVec4(0.47f, 0.75f, 1.0f, 0.85f), 2.0f};
        case kEdgeEvent:
            return {ImVec4(0.92f, 0.47f, 0.43f, 0.85f), 2.0f};
        case kEdgeOutcome:
            return {ImVec4(0.95f, 0.78f, 0.35f, 0.9f), 2.0f};
        case kEdgeReturn:
            return {ImVec4(0.55f, 0.58f, 0.62f, 0.55f), 1.5f};
        default:
            return {ImVec4(0.62f, 0.75f, 0.9f, 0.9f), 2.6f};
    }
}

const char* edge_kind_name(int kind)
{
    switch (kind) {
        case kEdgeFlow:     return "engine rule";
        case kEdgeEvent:    return "engine event";
        case kEdgeOutcome:  return "conditional exit";
        case kEdgeReturn:   return "automatic return";
        default:            return "authored transition clip";
    }
}

void draw_node(const VNode& node)
{
    const ImU32 header =
        node.is_any_state ? IM_COL32(168, 74, 66, 235)
                          : category_color(node.cat, node.is_transition);
    ed::PushStyleColor(ed::StyleColor_NodeBg,
                       ImVec4(0.125f, 0.14f, 0.16f, 0.92f));
    int extra_style = 0;
    if (node.is_any_state) {
        ed::PushStyleColor(ed::StyleColor_NodeBorder,
                           ImVec4(0.92f, 0.47f, 0.43f, 0.9f));
        extra_style = 1;
    } else if (node.is_default) {
        ed::PushStyleColor(ed::StyleColor_NodeBorder,
                           ImVec4(0.45f, 0.85f, 1.0f, 0.9f));
        extra_style = 1;
    } else if (node.overrides_base) {
        ed::PushStyleColor(ed::StyleColor_NodeBorder,
                           ImVec4(0.95f, 0.78f, 0.25f, 0.7f));
        extra_style = 1;
    }
    ed::BeginNode(ed::NodeId((uintptr_t)node.id));
    ImGui::PushID(node.id);

    const float base_w = node.is_transition ? 250.0f : 330.0f;
    const float title_w =
        ImGui::CalcTextSize(node.title.c_str()).x + 64.0f;
    const float content_w = std::min(480.0f, std::max(base_w, title_w));
    const float content_x = ImGui::GetCursorPosX();

    // Header row: in-pin, title, out-pin.
    {
        const ImVec2 icon_size(20.0f, 20.0f);
        ed::BeginPin(ed::PinId((uintptr_t)pin_in(node.id)),
                     ed::PinKind::Input);
        ax::Widgets::Icon(icon_size, ax::Drawing::IconType::Flow, false,
                          ImVec4(0.85f, 0.88f, 0.92f, 1.0f),
                          ImVec4(0.12f, 0.13f, 0.15f, 0.9f));
        {
            const ImVec2 mn = ImGui::GetItemRectMin();
            const ImVec2 mx = ImGui::GetItemRectMax();
            ed::PinRect(mn, mx);
            const float cy = (mn.y + mx.y) * 0.5f;
            ed::PinPivotRect(ImVec2(mn.x, cy), ImVec2(mn.x, cy));
        }
        ed::EndPin();
        ImGui::SameLine(0.0f, 6.0f);
        ImGui::TextColored(ImVec4(0.96f, 0.97f, 0.99f, 1.0f), "%s",
                           node.title.c_str());
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::SetCursorPosX(content_x + content_w - 20.0f);
        ed::BeginPin(ed::PinId((uintptr_t)pin_out(node.id)),
                     ed::PinKind::Output);
        ax::Widgets::Icon(icon_size, ax::Drawing::IconType::Flow, false,
                          ImVec4(0.85f, 0.88f, 0.92f, 1.0f),
                          ImVec4(0.12f, 0.13f, 0.15f, 0.9f));
        {
            const ImVec2 mn = ImGui::GetItemRectMin();
            const ImVec2 mx = ImGui::GetItemRectMax();
            ed::PinRect(mn, mx);
            const float cy = (mn.y + mx.y) * 0.5f;
            ed::PinPivotRect(ImVec2(mx.x, cy), ImVec2(mx.x, cy));
        }
        ed::EndPin();
    }
    const float header_bottom = ImGui::GetItemRectMax().y + 4.0f;
    if (!node.subtitle.empty()) {
        ImGui::SetCursorPosX(content_x);
        ImGui::TextDisabled("%s", node.subtitle.c_str());
    }
    ImGui::Dummy(ImVec2(content_w, 2.0f));

    const size_t shown = std::min<size_t>(node.rows.size(), 8);
    for (size_t i = 0; i < shown; ++i) {
        const Row& row = node.rows[i];
        ImGui::SetCursorPosX(content_x);
        ImGui::TextDisabled("%s", row.label.c_str());
        ImGui::SameLine(0.0f, 8.0f);
        if (row.duration > 0.0f) {
            ImGui::Text("%s  (%.2fs)", row.clip.c_str(), row.duration);
        } else {
            ImGui::TextUnformatted(row.clip.c_str());
        }
    }
    if (node.rows.size() > shown) {
        ImGui::SetCursorPosX(content_x);
        ImGui::TextDisabled("+%d more...",
                            (int)(node.rows.size() - shown));
    }
    ImGui::SetCursorPosX(content_x);
    ImGui::Dummy(ImVec2(content_w, 1.0f));

    ImGui::PopID();
    ed::EndNode();
    ed::PopStyleColor(1 + extra_style);

    if (ImGui::IsItemVisible()) {
        ImDrawList* dl = ed::GetNodeBackgroundDrawList(
            ed::NodeId((uintptr_t)node.id));
        if (dl) {
            const ImVec2 node_min = ImGui::GetItemRectMin();
            const ImVec2 node_max = ImGui::GetItemRectMax();
            const float border = ed::GetStyle().NodeBorderWidth * 0.5f;
            dl->AddRectFilled(
                ImVec2(node_min.x + border, node_min.y + border),
                ImVec2(node_max.x - border,
                       std::min(header_bottom, node_max.y)),
                header, ed::GetStyle().NodeRounding,
                ImDrawFlags_RoundCornersTop);
        }
    }
}

void draw_inspector()
{
    const VNode* node = nullptr;
    for (const VNode& candidate : g.nodes) {
        if (candidate.id == g.selected) {
            node = &candidate;
            break;
        }
    }
    if (!node) {
        ImGui::TextDisabled("Select a state to inspect it.");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextWrapped(
            "States come from the entity's AnimationManagerComponent in "
            "the game databases. Into / Loop / Out rows are the engine's "
            "state grammar; wires show authored transition clips.");
        if (!g.tree.chain.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.55f, 0.9f, 1.0f, 1.0f),
                               "Inheritance chain (%d record(s))",
                               (int)g.tree.chain.size());
            for (size_t i = 0; i < g.tree.chain.size(); ++i) {
                ImGui::TextDisabled("%d. 0x%08X", (int)i,
                                    g.tree.chain[i]);
            }
        }
        return;
    }

    ImGui::TextColored(ImVec4(0.96f, 0.97f, 0.99f, 1.0f), "%s",
                       node->title.c_str());
    ImGui::TextDisabled("%s%s",
                        node->is_transition ? "Transition - " : "State - ",
                        kCategoryNames[(int)node->cat]);
    if (node->is_default) {
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 1.0f, 1.0f),
                           "Default state");
    }
    if (node->overrides_base) {
        ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.25f, 1.0f),
                           "Overrides an inherited set");
    }
    ImGui::Separator();
    for (const Row& row : node->rows) {
        ImGui::TextDisabled("%s", row.label.c_str());
        ImGui::SameLine(0.0f, 8.0f);
        ImGui::TextUnformatted(row.clip.c_str());
        if (row.duration > 0.0f) {
            ImGui::SameLine(0.0f, 8.0f);
            ImGui::TextDisabled("%.2fs", row.duration);
        }
        ImGui::TextDisabled("   key %08X", row.key);
        if (row.slot_index >= 0 &&
            row.slot_index < (int)g.tree.slots.size()) {
            const Gdb::AnimTreeSlot& slot =
                g.tree.slots[(size_t)row.slot_index];
            ImGui::SameLine();
            if (row.chain_depth == 0 && g.tree.chain.size() > 1) {
                ImGui::TextDisabled("- local override");
            } else if (row.chain_depth > 0) {
                ImGui::TextDisabled("- inherited (depth %d, 0x%08X)",
                                    row.chain_depth, slot.owner_record);
            }
        }
    }

    auto title_of = [&](int node_id) -> std::string {
        for (const VNode& other : g.nodes) {
            if (other.id == node_id) return other.title;
        }
        return "?";
    };
    bool wrote_header = false;
    for (const VLink& link : g.links) {
        if (link.to_node != node->id) continue;
        if (!wrote_header) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.55f, 0.9f, 1.0f, 1.0f),
                               "Entered from");
            wrote_header = true;
        }
        const EdgeStyle style = edge_style(link.kind);
        ImGui::TextColored(style.color, "%s",
                           title_of(link.from_node).c_str());
        if (!link.label.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("- %s", link.label.c_str());
        }
    }
    wrote_header = false;
    for (const VLink& link : g.links) {
        if (link.from_node != node->id) continue;
        if (!wrote_header) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.55f, 0.9f, 1.0f, 1.0f),
                               "Leads to");
            wrote_header = true;
        }
        const EdgeStyle style = edge_style(link.kind);
        ImGui::TextColored(style.color, "%s",
                           title_of(link.to_node).c_str());
        if (!link.label.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("- %s", link.label.c_str());
        }
    }
}

}

bool Available(uint32_t entity_hash)
{
    return entity_hash != 0 &&
           g_global_entity_anim_trees.count(entity_hash) != 0;
}

void Open(uint32_t entity_hash, const std::string& title)
{
    const auto it = g_global_entity_anim_trees.find(entity_hash);
    if (it == g_global_entity_anim_trees.end()) {
        OutputLog::warn("animation tree: no indexed set for entity " +
                        title);
        return;
    }
    g.entity = entity_hash;
    g.title = title.empty() ? "Entity" : title;
    g.tree = it->second;
    g.filter.clear();
    for (bool& visible : g.cat_visible) visible = true;
    g.show_transitions = true;
    build_graph();
    g.open = true;
    g.bring_front = true;
    OutputLog::info(
        "animation tree: " + g.title + " - " +
        std::to_string(g.tree.slots.size()) + " slot(s), " +
        std::to_string(g.nodes.size()) + " node(s), chain depth " +
        std::to_string(g.tree.chain.size()));
}

void Draw()
{
    if (!g.open) return;
    if (g.bring_front) {
        ImGui::SetNextWindowFocus();
        g.bring_front = false;
    }
    ImGui::SetNextWindowSize(ImVec2(1240.0f, 780.0f),
                             ImGuiCond_FirstUseEver);
    const std::string window_title =
        "Animation Tree - " + g.title + "###anim_tree_window";
    if (!ImGui::Begin(window_title.c_str(), &g.open)) {
        ImGui::End();
        return;
    }

    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputTextWithHint("##anim_tree_filter", "Filter states...",
                             &g.filter);
    ImGui::SameLine();
    if (ImGui::Button("Fit view")) {
        g.fit_pending = true;
    }
    ImGui::SameLine();
    ImGui::Checkbox("Transitions", &g.show_transitions);
    ImGui::SameLine();
    ImGui::Checkbox("Conditions", &g.show_conditions);
    if (!S.hide_tooltips && ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Wires the states together with the engine's known trigger\n"
            "rules (movement, attacks, hits, action enter/exit). Solid\n"
            "bright wires are authored transition clips from the game\n"
            "data; colored wires are engine behaviour. Select or hover a\n"
            "state to see its condition labels; hover a wire for detail.");
    }
    ImGui::SameLine();
    ImGui::Checkbox("Returns", &g.show_returns);
    if (!S.hide_tooltips && ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Also draw the automatic back-to-idle wires (finished /\n"
            "recovers). Hidden by default to keep the graph readable.");
    }
    ImGui::SameLine();
    ImGui::Checkbox("All labels", &g.all_labels);
    for (int cat = 0; cat < (int)Category::Count; ++cat) {
        ImGui::SameLine();
        ImGui::PushID(cat);
        ImGui::PushStyleColor(
            ImGuiCol_Text,
            g.cat_visible[cat] ? ImGui::ColorConvertU32ToFloat4(
                                     category_color((Category)cat, false))
                               : ImVec4(0.45f, 0.47f, 0.5f, 1.0f));
        ImGui::Checkbox(kCategoryNames[cat], &g.cat_visible[cat]);
        ImGui::PopStyleColor();
        ImGui::PopID();
    }

    const std::string filter = lowercase(g.filter);
    std::vector<uint8_t> visible(g.nodes.size(), 0);
    int visible_count = 0;
    for (size_t i = 0; i < g.nodes.size(); ++i) {
        const VNode& node = g.nodes[i];
        if (!g.cat_visible[(int)node.cat]) continue;
        if (node.is_transition && !g.show_transitions) continue;
        if (!node_matches_filter(node, filter)) continue;
        visible[i] = 1;
        ++visible_count;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%d / %d nodes", visible_count,
                        (int)g.nodes.size());

    const float inspector_w = 330.0f;
    ImGui::BeginChild("anim_tree_canvas",
                      ImVec2(-inspector_w - 8.0f, 0.0f), false,
                      ImGuiWindowFlags_NoScrollbar);
    if (!g.ctx) {
        ed::Config config;
        config.SettingsFile = nullptr;
        config.CanvasSizeMode = ed::CanvasSizeMode::CenterOnly;
        g.ctx = ed::CreateEditor(&config);
        ed::SetCurrentEditor(g.ctx);
        ed::GetStyle().SourceDirection = ImVec2(1.0f, 0.0f);
        ed::GetStyle().TargetDirection = ImVec2(-1.0f, 0.0f);
        ed::SetCurrentEditor(nullptr);
        g.layout_pending = true;
        g.fit_pending = true;
    }
    ed::SetCurrentEditor(g.ctx);
    ed::Begin("##anim_tree_canvas_ed");

    std::unordered_map<int, uint8_t> drawn;
    drawn.reserve(g.nodes.size() * 2 + 1);
    for (size_t i = 0; i < g.nodes.size(); ++i) {
        if (!visible[i]) continue;
        VNode& node = g.nodes[i];
        if (g.layout_pending) {
            ed::SetNodePosition(ed::NodeId((uintptr_t)node.id),
                                ImVec2(node.x, node.y));
        }
        draw_node(node);
        drawn[node.id] = 1;
    }
    g.layout_pending = false;

    const int hovered_node = (int)ed::GetHoveredNode().Get();

    struct EdgeLabel {
        ImVec2 canvas_pos;
        std::string text;
        int kind;
    };
    std::vector<EdgeLabel> labels;
    for (const VLink& link : g.links) {
        if (!drawn.count(link.from_node) || !drawn.count(link.to_node)) {
            continue;
        }
        if (!g.show_conditions && link.kind != kEdgeAuthored) continue;
        if (link.kind == kEdgeReturn && !g.show_returns) continue;
        const EdgeStyle style = edge_style(link.kind);
        ed::Link(ed::LinkId((uintptr_t)link.id),
                 ed::PinId((uintptr_t)pin_out(link.from_node)),
                 ed::PinId((uintptr_t)pin_in(link.to_node)),
                 style.color, style.thickness);
        const bool touches_focus =
            link.from_node == g.selected || link.to_node == g.selected ||
            (hovered_node != 0 && (link.from_node == hovered_node ||
                                   link.to_node == hovered_node));
        if (!link.label.empty() && g.show_conditions &&
            (g.all_labels || touches_focus)) {
            const ed::NodeId from_id((uintptr_t)link.from_node);
            const ed::NodeId to_id((uintptr_t)link.to_node);
            const ImVec2 fp = ed::GetNodePosition(from_id);
            const ImVec2 fs = ed::GetNodeSize(from_id);
            const ImVec2 tp = ed::GetNodePosition(to_id);
            const ImVec2 ts = ed::GetNodeSize(to_id);
            const ImVec2 a(fp.x + fs.x, fp.y + fs.y * 0.5f);
            const ImVec2 b(tp.x, tp.y + ts.y * 0.5f);
            EdgeLabel label;
            label.canvas_pos = ImVec2(a.x + (b.x - a.x) * 0.35f,
                                      a.y + (b.y - a.y) * 0.35f);
            label.text = link.label;
            label.kind = link.kind;
            labels.push_back(std::move(label));
        }
    }

    {
        const ed::LinkId hovered_link = ed::GetHoveredLink();
        if (hovered_link) {
            const int link_id = (int)hovered_link.Get();
            for (const VLink& link : g.links) {
                if (link.id != link_id) continue;
                auto node_title = [&](int node_id) -> const char* {
                    for (const VNode& node : g.nodes) {
                        if (node.id == node_id) return node.title.c_str();
                    }
                    return "?";
                };
                ed::Suspend();
                ImGui::BeginTooltip();
                ImGui::Text("%s  ->  %s", node_title(link.from_node),
                            node_title(link.to_node));
                if (!link.label.empty()) {
                    const EdgeStyle style = edge_style(link.kind);
                    ImGui::TextColored(style.color, "%s",
                                       link.label.c_str());
                }
                ImGui::TextDisabled("%s", edge_kind_name(link.kind));
                ImGui::EndTooltip();
                ed::Resume();
                break;
            }
        }
    }

    if (!labels.empty()) {
        const ImVec2 origin_a = ed::CanvasToScreen(ImVec2(0.0f, 0.0f));
        const ImVec2 origin_b = ed::CanvasToScreen(ImVec2(100.0f, 0.0f));
        const float zoom = (origin_b.x - origin_a.x) / 100.0f;
        if (zoom >= 0.22f) {
            ed::Suspend();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 clip_min = ImGui::GetWindowPos();
            const ImVec2 clip_max(clip_min.x + ImGui::GetWindowWidth(),
                                  clip_min.y + ImGui::GetWindowHeight());
            dl->PushClipRect(clip_min, clip_max, true);
            const float alpha = std::min(1.0f, (zoom - 0.18f) * 4.0f);
            std::vector<ImVec4> placed;
            placed.reserve(labels.size());
            for (const EdgeLabel& label : labels) {
                ImVec2 at = ed::CanvasToScreen(label.canvas_pos);
                if (at.x < clip_min.x - 240.0f || at.x > clip_max.x ||
                    at.y < clip_min.y - 60.0f ||
                    at.y > clip_max.y + 60.0f) {
                    continue;
                }
                const ImVec2 size =
                    ImGui::CalcTextSize(label.text.c_str());
                const ImVec2 pad(5.0f, 2.0f);
                const float w = size.x + pad.x * 2.0f;
                const float h = size.y + pad.y * 2.0f;
                for (int guard = 0; guard < 24; ++guard) {
                    bool collides = false;
                    for (const ImVec4& rect : placed) {
                        if (at.x - w * 0.5f < rect.z &&
                            at.x + w * 0.5f > rect.x &&
                            at.y - h * 0.5f < rect.w &&
                            at.y + h * 0.5f > rect.y) {
                            at.y = rect.w + h * 0.5f + 2.0f;
                            collides = true;
                            break;
                        }
                    }
                    if (!collides) break;
                }
                const ImVec2 mn(at.x - w * 0.5f, at.y - h * 0.5f);
                const ImVec2 mx(at.x + w * 0.5f, at.y + h * 0.5f);
                placed.push_back(ImVec4(mn.x, mn.y, mx.x, mx.y));
                dl->AddRectFilled(
                    mn, mx,
                    IM_COL32(24, 27, 33, (int)(232 * alpha)), 4.0f);
                const EdgeStyle style = edge_style(label.kind);
                const ImU32 text_col = ImGui::ColorConvertFloat4ToU32(
                    ImVec4(style.color.x, style.color.y, style.color.z,
                           alpha));
                dl->AddText(ImVec2(mn.x + pad.x, mn.y + pad.y), text_col,
                            label.text.c_str());
            }
            dl->PopClipRect();
            ed::Resume();
        }
    }

    for (VNode& node : g.nodes) {
        if (!drawn.count(node.id)) continue;
        const ImVec2 pos =
            ed::GetNodePosition(ed::NodeId((uintptr_t)node.id));
        node.x = pos.x;
        node.y = pos.y;
    }

    if (g.fit_pending && visible_count > 0) {
        ed::NavigateToContent();
        g.fit_pending = false;
    }

    {
        ed::NodeId selected[1] = {};
        if (ed::GetSelectedNodes(selected, 1) > 0) {
            g.selected = (int)selected[0].Get();
        } else if (ed::GetSelectedObjectCount() == 0) {
            g.selected = 0;
        }
    }

    ed::End();
    ed::SetCurrentEditor(nullptr);
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("anim_tree_inspector", ImVec2(inspector_w, 0.0f),
                      true);
    draw_inspector();
    ImGui::EndChild();

    ImGui::End();
}

void Shutdown()
{
    if (g.ctx) {
        ed::DestroyEditor(g.ctx);
        g.ctx = nullptr;
    }
    g.open = false;
}

}
