#include "QuestNodeView.h"

#include "Blueprint/BlueprintEditor.h"
#include "../../Quest/QuestAuthoring.h"
#include "../../Quest/QuestGraph.h"
#include "../../Quest/QuestReferences.h"
#include "../../Quest/QuestRewards.h"
#include "../../Quest/QuestStoryGraph.h"
#include "../../Quest/QuestWorldIndex.h"
#include "../../Level/Core/LevelLoader.h"
#include "../../Level/Editing/LevelEdit.h"
#include "../../Level/Database/TextBank.h"
#include "../../Utilities/State.h"
#include "../../animations/AnimBank.h"

#include "imgui.h"
#include "imgui_stdlib.h"
#include "imgui_node_editor.h"
#include "IconsFontAwesome6.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace QuestUI {
namespace {

namespace ed = ax::NodeEditor;

std::mutex g_graph_mutex;
std::shared_ptr<const Quest::Graph> g_graph;
std::uint64_t g_graph_generation = 0;

ed::EditorContext* g_editor = nullptr;
std::uint64_t g_visible_generation = 0;
bool g_apply_layout = false;
bool g_fit_requested = false;
bool g_focus_quest_requested = false;
bool g_initial_view_all = false;
std::size_t g_initial_focus_node_count = 2;
std::size_t g_initial_focus_node_start = 0;
bool g_initial_focus_explicit_range = false;
bool g_select_completion_requested = false;
std::size_t g_completion_selection_index = 0;
int g_completion_focus_frames = 0;

std::vector<Quest::AuthoredQuest> g_authored_quests;
int g_active_authored_quest = -1;
int g_place_authored_node = 0;
int g_context_authored_node = 0;
int g_selected_authored_node = 0;
int g_selected_graph_node = 0;
int g_level_reference_node = 0;
bool g_open_prerequisite_menu = false;
bool g_prerequisite_capture_scroll_bottom = false;
float g_prerequisite_capture_scroll_fraction = -1.0f;
ImVec2 g_create_node_position(0.0f, 0.0f);
char g_create_node_filter[128]{};
LevelReferenceTarget g_level_reference_target =
    LevelReferenceTarget::QuestGiver;
NpcCreationRequest g_pending_npc_creation;
std::string g_pending_npc_quest_id;
int g_pending_npc_node = 0;
char g_npc_creation_filter[128]{};
char g_npc_instance_filter[128]{};

constexpr float kNodeContentWidth = 500.0f;



constexpr std::size_t kMaxVisibleNodeDetails = 12;

Quest::AuthoredQuest* active_authored_quest() {
    if (g_active_authored_quest < 0 ||
        static_cast<std::size_t>(g_active_authored_quest) >=
            g_authored_quests.size()) return nullptr;
    return &g_authored_quests[static_cast<std::size_t>(
        g_active_authored_quest)];
}

const Quest::AuthoredQuest* active_authored_quest_const() {
    return active_authored_quest();
}

void refresh_authored_lua() {
    if (!BlueprintUI::IsActive()) return;
    S.lua_preview_content = BlueprintUI::ActiveLua();
}

std::string g_reference_root;
std::vector<std::string> g_audio_assets;
std::unordered_map<std::string, std::vector<std::string>> g_audio_by_dialogue;
std::vector<std::string> g_level_assets;
std::unordered_map<std::string, std::vector<Quest::WorldEntityPlacement>>
    g_world_entities;
std::unordered_set<std::string> g_world_queries;
std::shared_ptr<const std::vector<uint8_t>> g_cutscene_database;
bool g_reference_ready = false;

std::shared_ptr<const std::vector<uint8_t>> load_cutscene_database(
    const std::string& root) {
    if (root.empty()) return {};
    const std::filesystem::path path =
        std::filesystem::path(root) / "data" / "interactivecutscenes" /
        "interactivecutscenes.gdb";
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    input.seekg(0, std::ios::end);
    const std::streamoff length = input.tellg();
    if (length <= 0 || length > std::streamoff(128 * 1024 * 1024)) return {};
    input.seekg(0, std::ios::beg);
    auto bytes = std::make_shared<std::vector<uint8_t>>(
        static_cast<std::size_t>(length));
    input.read(reinterpret_cast<char*>(bytes->data()),
               static_cast<std::streamsize>(length));
    if (!input) return {};
    return bytes;
}

ed::NodeId node_id(int id) {
    return ed::NodeId(static_cast<std::uintptr_t>(id));
}

ed::PinId input_pin_id(int id) {
    return ed::PinId(static_cast<std::uintptr_t>(100000 + id * 2));
}

ed::PinId output_pin_id(int id) {
    return ed::PinId(static_cast<std::uintptr_t>(100001 + id * 2));
}

ed::LinkId link_id(int id) {
    return ed::LinkId(static_cast<std::uintptr_t>(200000 + id));
}

ImVec4 node_color(Quest::NodeKind kind) {
    switch (kind) {
        case Quest::NodeKind::Quest:
            return ImVec4(0.12f, 0.27f, 0.43f, 0.98f);
        case Quest::NodeKind::Thread:
            return ImVec4(0.10f, 0.35f, 0.34f, 0.98f);
        case Quest::NodeKind::Function:
            return ImVec4(0.31f, 0.20f, 0.43f, 0.98f);
        case Quest::NodeKind::State:
            return ImVec4(0.48f, 0.27f, 0.10f, 0.98f);
        case Quest::NodeKind::Action:
            return ImVec4(0.18f, 0.38f, 0.22f, 0.98f);
    }
    return ImVec4(0.20f, 0.20f, 0.23f, 0.98f);
}

ImVec4 link_color(const Quest::GraphNode* target) {
    if (!target) return ImVec4(0.65f, 0.68f, 0.75f, 0.85f);
    ImVec4 color = node_color(target->kind);
    color.x = (color.x + 0.8f) * 0.5f;
    color.y = (color.y + 0.8f) * 0.5f;
    color.z = (color.z + 0.8f) * 0.5f;
    color.w = 0.85f;
    return color;
}

void ensure_editor() {
    if (g_editor) return;
    ed::Config config;
    config.SettingsFile = nullptr;
    config.CanvasSizeMode = ed::CanvasSizeMode::CenterOnly;
    g_editor = ed::CreateEditor(&config);
    ed::SetCurrentEditor(g_editor);
    ed::GetStyle().SourceDirection = ImVec2(1.0f, 0.0f);
    ed::GetStyle().TargetDirection = ImVec2(-1.0f, 0.0f);
    ed::SetCurrentEditor(nullptr);
}

void reset_editor() {
    if (g_editor) {
        ed::DestroyEditor(g_editor);
        g_editor = nullptr;
    }
    ensure_editor();
    g_selected_graph_node = 0;
    g_apply_layout = true;
    g_fit_requested = g_initial_view_all;
    g_focus_quest_requested = !g_initial_view_all;
}



void draw_exec_pin_triangle(const ImVec2& min, const ImVec2& max,
                            bool hovered) {
    const bool held = hovered &&
        ImGui::IsMouseDown(ImGuiMouseButton_Left);
    const ImU32 colour = held ? IM_COL32(255, 235, 130, 255)
        : hovered ? IM_COL32(255, 210, 95, 255)
                  : IM_COL32(205, 225, 240, 255);
    const float inset_x = (max.x - min.x - 18.0f) * 0.5f;
    const float inset_y = (max.y - min.y - 18.0f) * 0.5f;
    ImGui::GetWindowDrawList()->AddTriangleFilled(
        ImVec2(min.x + inset_x + 4.0f, min.y + inset_y + 3.0f),
        ImVec2(max.x - inset_x - 3.0f, (min.y + max.y) * 0.5f),
        ImVec2(min.x + inset_x + 4.0f, max.y - inset_y - 3.0f),
        colour);
}

constexpr float kExecPinSize = 26.0f;

void draw_input_pin(int id, float content_x) {
    ImGui::SetCursorPosX(content_x);
    ImGui::Dummy(ImVec2(kExecPinSize, kExecPinSize));
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const ImVec2 pivot(min.x, (min.y + max.y) * 0.5f);
    ed::BeginPin(input_pin_id(id), ed::PinKind::Input);
    ed::PinRect(min, max);
    ed::PinPivotRect(pivot, pivot);
    ed::EndPin();
    const bool hovered = ImGui::IsMouseHoveringRect(min, max);
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    draw_exec_pin_triangle(min, max, hovered);
}

void draw_output_pin(int id, float content_x) {
    ImGui::SameLine();
    ImGui::SetCursorPosX(content_x + kNodeContentWidth - kExecPinSize);
    ImGui::Dummy(ImVec2(kExecPinSize, kExecPinSize));
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const ImVec2 pivot(max.x, (min.y + max.y) * 0.5f);
    ed::BeginPin(output_pin_id(id), ed::PinKind::Output);
    ed::PinRect(min, max);
    ed::PinPivotRect(pivot, pivot);
    ed::EndPin();
    const bool hovered = ImGui::IsMouseHoveringRect(min, max);
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    draw_exec_pin_triangle(min, max, hovered);
}

void draw_node(const Quest::GraphNode& node) {
    const ed::NodeId id = node_id(node.id);
    ed::PushStyleColor(ed::StyleColor_NodeBg, node_color(node.kind));
    ed::PushStyleColor(ed::StyleColor_NodeBorder,
                       ImVec4(0.66f, 0.72f, 0.82f, 0.75f));
    ed::BeginNode(id);
    ImGui::PushID(node.id);

    const float content_x = ImGui::GetCursorPosX();
    draw_input_pin(node.id, content_x);
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.76f, 0.82f, 0.92f, 1.0f), "%s",
                       node.badge.empty() ? Quest::NodeKindName(node.kind)
                                          : node.badge.c_str());
    draw_output_pin(node.id, content_x);

    ImGui::SetCursorPosX(content_x);
    ImGui::PushTextWrapPos(content_x + kNodeContentWidth);
    ImGui::TextUnformatted(node.title.c_str());
    if (!node.subtitle.empty()) {
        ImGui::TextDisabled("%s", node.subtitle.c_str());
    }
    ImGui::Separator();
    std::size_t shown = 0;
    for (const std::string& detail : node.details) {
        if (shown == kMaxVisibleNodeDetails) {
            ImGui::TextDisabled(
                "+ %zu more - select the node to view everything",
                node.details.size() - shown);
            break;
        }
        const bool indented = detail.rfind("   ", 0) == 0 ||
                              detail.rfind("  ", 0) == 0;
        const bool dialogue = indented &&
                              detail.find(": \"") != std::string::npos;
        if (dialogue) {
            ImGui::PushStyleColor(
                ImGuiCol_Text, ImVec4(0.94f, 0.95f, 0.98f, 1.0f));
            ImGui::TextWrapped("%s", detail.c_str());
            ImGui::PopStyleColor();
        } else if (indented) {
            ImGui::TextDisabled("%s", detail.c_str());
        } else {
            ImGui::TextWrapped("%s", detail.c_str());
        }
        ++shown;
    }
    ImGui::PopTextWrapPos();

    ImGui::SetCursorPosX(content_x);
    ImGui::Dummy(ImVec2(kNodeContentWidth, 1.0f));

    ImGui::PopID();
    ed::EndNode();
    ed::PopStyleColor(2);

    if (g_apply_layout) {
        ed::SetNodePosition(id, ImVec2(node.x, node.y));
    }
}

std::string story_milestone_display_name(
    const Quest::StoryProgressMilestone& milestone,
    std::size_t sequence_index,
    std::size_t sequence_size) {
    std::string title;
    if (milestone.title_tag) {
        TextBank::LookupTag(milestone.title_tag, title);
    }
    if (title.empty()) title = milestone.fallback_title;
    if (milestone.stage_detail && milestone.stage_detail[0]) {
        title += " - ";
        title += milestone.stage_detail;
    }
    if (sequence_index > 0 && sequence_index + 1 < sequence_size) {
        title = std::to_string(sequence_index) + ". " + title;
    }
    return title;
}

std::string story_milestone_display_name(const std::string& value) {
    const auto& milestones = Quest::StoryProgressMilestones();
    for (std::size_t index = 0; index < milestones.size(); ++index) {
        if (value == milestones[index].value) {
            return story_milestone_display_name(
                milestones[index], index, milestones.size());
        }
    }
    return "Unknown story stage";
}

bool draw_story_milestone_combo(const char* label, std::string& value,
                                bool no_expiry_at_story_end = false) {
    bool changed = false;
    ImGui::TextUnformatted(label);
    ImGui::PushID(label);
    ImGui::SetNextItemWidth(-1.0f);
    const bool no_expiry = no_expiry_at_story_end &&
        value == "ScriptEnum.GAMEFLOW_END";
    const std::string preview = no_expiry
        ? "None"
        : story_milestone_display_name(value);
    if (ImGui::BeginCombo("##milestone", preview.c_str())) {
        const auto& milestones = Quest::StoryProgressMilestones();
        for (std::size_t index = 0; index < milestones.size(); ++index) {
            const Quest::StoryProgressMilestone& milestone = milestones[index];
            const bool selected = value == milestone.value;
            const bool is_no_expiry = no_expiry_at_story_end &&
                std::string(milestone.value) == "ScriptEnum.GAMEFLOW_END";
            const std::string display = is_no_expiry
                ? "None"
                : story_milestone_display_name(
                      milestone, index, milestones.size());
            if (ImGui::Selectable(display.c_str(), selected)) {
                value = milestone.value;
                changed = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::PopID();
    return changed;
}


bool contains_case_insensitive(std::string_view text,
                               std::string_view query) {
    if (query.empty()) return true;
    std::string haystack(text);
    std::string needle(query);
    auto lower = [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    };
    std::transform(haystack.begin(), haystack.end(), haystack.begin(), lower);
    std::transform(needle.begin(), needle.end(), needle.begin(), lower);
    return haystack.find(needle) != std::string::npos;
}

ed::LinkId authored_link_id(int id) {
    return ed::LinkId(static_cast<std::uintptr_t>(600000 + id));
}

const char* authored_node_badge(Quest::AuthoredNodeKind kind) {
    if (Quest::IsPrerequisiteNode(kind)) return "Prerequisite";
    switch (kind) {
        case Quest::AuthoredNodeKind::QuestStart: return "Quest start";
        case Quest::AuthoredNodeKind::ApproachNpc: return "Trigger";
        case Quest::AuthoredNodeKind::Dialogue: return "Dialogue";
        case Quest::AuthoredNodeKind::AcceptQuest: return "Choice";
        case Quest::AuthoredNodeKind::HoldInteraction:
            return "Interaction";
        case Quest::AuthoredNodeKind::ObtainItem: return "Objective";
        case Quest::AuthoredNodeKind::ReturnToNpc: return "Objective";
        case Quest::AuthoredNodeKind::CompleteQuest: return "Completion";
        case Quest::AuthoredNodeKind::SkipChildhoodEnding:
            return "Existing quest action";
        default: return "Quest";
    }
}

bool authored_node_is_terminal(Quest::AuthoredNodeKind kind) {
    return kind == Quest::AuthoredNodeKind::CompleteQuest ||
           kind == Quest::AuthoredNodeKind::SkipChildhoodEnding;
}

ImVec4 authored_node_colour(Quest::AuthoredNodeKind kind) {
    if (Quest::IsPrerequisiteNode(kind)) {
        return node_color(Quest::NodeKind::State);
    }
    switch (kind) {
        case Quest::AuthoredNodeKind::Dialogue:
            return node_color(Quest::NodeKind::Thread);
        case Quest::AuthoredNodeKind::AcceptQuest:
            return node_color(Quest::NodeKind::Quest);
        case Quest::AuthoredNodeKind::HoldInteraction:
            return node_color(Quest::NodeKind::Quest);
        case Quest::AuthoredNodeKind::ObtainItem:
            return node_color(Quest::NodeKind::State);
        case Quest::AuthoredNodeKind::SkipChildhoodEnding:
            return node_color(Quest::NodeKind::Action);
        default:
            return node_color(Quest::NodeKind::Action);
    }
}

std::string authored_node_summary(const Quest::AuthoredNode& node) {
    switch (node.kind) {
        case Quest::AuthoredNodeKind::QuestStart:
            return {};
        case Quest::AuthoredNodeKind::ApproachNpc:
            return node.entity.valid()
                ? node.entity.entity_name + " within " +
                      std::to_string(node.approach_radius) + " units"
                : "Entity not assigned";
        case Quest::AuthoredNodeKind::Dialogue:
            if (node.text.empty()) return "Dialogue text not entered";
            return node.entity.valid()
                ? node.entity.entity_name + ": \"" + node.text + "\""
                : "Speaker not assigned: \"" + node.text + "\"";
        case Quest::AuthoredNodeKind::AcceptQuest:
            return node.text.empty() ? "Question not entered" : node.text;
        case Quest::AuthoredNodeKind::HoldInteraction:
            return node.text.empty() ? "Prompt text not entered" : node.text;
        case Quest::AuthoredNodeKind::ObtainItem:
            return node.item.valid()
                ? "Get " + std::to_string(node.item_count) + " x " +
                      node.item.display_name
                : "Item not selected";
        case Quest::AuthoredNodeKind::ReturnToNpc:
            return node.entity.valid() ? "Return to " + node.entity.entity_name
                                       : "NPC not assigned";
        case Quest::AuthoredNodeKind::CompleteQuest:
            return "Finish the quest and grant its rewards";
        case Quest::AuthoredNodeKind::SkipChildhoodEnding:
            return "Run QC010's ending movie, cleanup, and adulthood handoff";
        case Quest::AuthoredNodeKind::PrerequisiteStoryProgress:
            return node.story_start.empty() ? "Progress range not set"
                                            : node.story_start + " to " +
                                                  node.story_end;
        case Quest::AuthoredNodeKind::PrerequisiteQuestState:
            return node.other_quest.empty()
                ? "Quest not selected"
                : node.other_quest + " is " +
                      Quest::RequiredQuestStateName(node.quest_state);
        case Quest::AuthoredNodeKind::PrerequisiteGameflowFlag:
            return node.gameflow_flag.empty() ? "Flag not entered"
                                              : node.gameflow_flag;
        case Quest::AuthoredNodeKind::PrerequisiteLuaCondition:
            return node.lua_condition.empty() ? "Condition not entered"
                                              : node.lua_condition;
        case Quest::AuthoredNodeKind::PrerequisiteHeroRequirement:
            return Quest::HeroRequirementKindName(node.hero_requirement);
    }
    return {};
}

bool decode_authored_pin(ed::PinId pin, int& node, bool& input) {
    const std::uintptr_t raw = pin.Get();
    if (raw < 100000) return false;
    const std::uintptr_t value = raw - 100000;
    node = static_cast<int>(value / 2);
    input = (value % 2) == 0;
    return node > 0;
}

bool draw_item_picker(const char* id, Quest::ItemReference& selected) {
    bool changed = false;
    const char* preview = selected.display_name.empty()
        ? "Select item" : selected.display_name.c_str();
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo(id, preview)) {
        static std::unordered_map<std::string, std::string> filters;
        std::string& filter = filters[id];
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##item_filter", "Search items...",
                                 &filter);
        ImGui::Separator();
        std::string needle = filter;
        std::transform(needle.begin(), needle.end(), needle.begin(),
                       [](unsigned char c) {
                           return static_cast<char>(std::tolower(c));
                       });
        ImGui::BeginChild("##item_rows", ImVec2(460.0f, 240.0f), false);
        for (const Gdb::ItemDetail& item : g_item_details) {
            if (item.is_money || item.internal_name.empty()) continue;
            const std::string display = item.display_name.empty()
                ? item.label : item.display_name;
            std::string haystack = display + ' ' + item.internal_name;
            std::transform(haystack.begin(), haystack.end(),
                           haystack.begin(), [](unsigned char c) {
                               return static_cast<char>(std::tolower(c));
                           });
            if (!needle.empty() &&
                haystack.find(needle) == std::string::npos) continue;
            const bool is_selected =
                selected.record_hash == item.record_hash;
            ImGui::PushID(static_cast<int>(item.record_hash));
            if (ImGui::Selectable(display.c_str(), is_selected)) {
                selected.record_hash = item.record_hash;
                selected.internal_name = item.internal_name;
                selected.display_name = display;
                selected.model_path = item.model_path;
                selected.source = Quest::WorldReference{};
                changed = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::EndCombo();
    }
    if (!selected.internal_name.empty()) {
        ImGui::TextDisabled("GDB item: %s", selected.internal_name.c_str());
    }
    return changed;
}

void draw_reference_summary(const Quest::WorldReference& reference) {
    if (!reference.valid()) {
        ImGui::TextDisabled("Not assigned");
        return;
    }
    ImGui::TextWrapped("%s", reference.entity_name.c_str());
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(
                                              ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", reference.level_id.c_str());
    ImGui::TextDisabled("XYZ  %.2f, %.2f, %.2f", reference.x,
                        reference.y, reference.z);
    ImGui::PopStyleColor();
}

std::vector<std::string> model_paths_for_hashes(
    const std::vector<uint32_t>& hashes) {
    std::vector<std::string> paths;
    for (uint32_t hash : hashes) {
        for (const FlatAssetEntry& model : S.all_mdl_files) {
            if (Anim::gdb_model_path_hash(model.full_path) != hash) continue;
            paths.push_back(model.full_path);
            break;
        }
    }
    return paths;
}

void draw_npc_gdb_summary(const Quest::WorldReference& reference) {
    if (reference.model_hashes.empty()) return;
    const std::vector<std::string> models =
        model_paths_for_hashes(reference.model_hashes);
    ImGui::TextUnformatted("GDB model / animation set");
    if (models.empty()) {
        ImGui::TextDisabled("%zu model hash(es)",
                            reference.model_hashes.size());
    } else {
        for (const std::string& model : models) {
            ImGui::TextWrapped("- %s", model.c_str());
        }
    }
    std::vector<std::string> animations;
    for (const Anim::ModelAnimationBinding& binding :
         Anim::model_animation_bindings()) {
        if (std::find(reference.model_hashes.begin(),
                      reference.model_hashes.end(),
                      binding.model_path_hash) ==
            reference.model_hashes.end()) continue;
        const std::string& name = binding.animation_name.empty()
            ? binding.source_name : binding.animation_name;
        if (!name.empty() &&
            std::find(animations.begin(), animations.end(), name) ==
                animations.end()) {
            animations.push_back(name);
        }
    }
    ImGui::TextDisabled("%zu authored animation binding(s)",
                        animations.size());
    const std::size_t shown = std::min<std::size_t>(animations.size(), 6);
    for (std::size_t i = 0; i < shown; ++i) {
        ImGui::TextWrapped("- %s", animations[i].c_str());
    }
    if (animations.size() > shown) {
        ImGui::TextDisabled("...and %zu more", animations.size() - shown);
    }
    ImGui::TextDisabled("Inherited from the selected NPC's GDB entity.");
}

}

bool CreateNewBlueprintQuest(const std::string& quest_id,
                             std::string& error) {
    error.clear();
    for (const Quest::AuthoredQuest& quest : g_authored_quests) {
        if (quest.quest_id == quest_id) {
            error = "A custom quest with this ID already exists.";
            return false;
        }
    }
    if (!BlueprintUI::CreateQuest(quest_id, error)) return false;
    g_active_authored_quest = -1;
    refresh_authored_lua();
    return true;
}

bool OpenAuthoredQuest(const std::string& quest_id) {
    if (!BlueprintUI::OpenQuest(quest_id)) return false;
    refresh_authored_lua();
    return true;
}

std::vector<std::string> AuthoredQuestIds() {
    return BlueprintUI::QuestIds();
}

bool DeleteAuthoredQuest(const std::string& quest_id, std::string& error) {
    return BlueprintUI::DeleteQuest(quest_id, error);
}

bool IsAuthoredQuestActive() {
    return BlueprintUI::IsActive();
}

std::string ActiveAuthoredQuestId() {
    return BlueprintUI::ActiveQuestId();
}

std::string ActiveAuthoredLua() {
    return BlueprintUI::ActiveLua();
}

std::string ActiveAuthoredQuestLua() {
    return BlueprintUI::ActiveQuestLua();
}

std::string ActiveAuthoredEligibilityLua() {
    return BlueprintUI::ActiveEligibilityLua();
}

std::vector<std::pair<std::string, std::string>>
ActiveAuthoredTextEntries() {
    return BlueprintUI::ActiveTextEntries();
}

bool ValidateActiveAuthoredQuest(std::string& error) {
    return BlueprintUI::ValidateActive(error);
}


void RequestOpenPrerequisiteMenu() {
    g_open_prerequisite_menu = true;
}

void AddPrerequisiteExamplesForCapture() {
    Quest::AuthoredQuest* quest = active_authored_quest();
    if (!quest) return;
    quest->prerequisites.clear();

    Quest::AddAuthoredPrerequisite(
        *quest, Quest::AuthoredNodeKind::PrerequisiteStoryProgress);
    Quest::AuthoredNode& story = quest->prerequisites.back();
    story.story_start = "ScriptEnum.DebugQC080";
    story.story_end = "ScriptEnum.GAMEFLOW_END";

    Quest::AddAuthoredPrerequisite(
        *quest, Quest::AuthoredNodeKind::PrerequisiteQuestState);
    Quest::AuthoredNode& state = quest->prerequisites.back();
    state.other_quest = "QO100_BrightwoodFarmer";
    state.quest_state = Quest::RequiredQuestState::Completed;
    state.expected = true;

    Quest::AddAuthoredPrerequisite(
        *quest, Quest::AuthoredNodeKind::PrerequisiteGameflowFlag);
    Quest::AuthoredNode& condition = quest->prerequisites.back();
    condition.gameflow_flag = "TempleOfEvilDestroyed";
    condition.expected = false;

    Quest::AddAuthoredPrerequisite(
        *quest, Quest::AuthoredNodeKind::PrerequisiteHeroRequirement);
    Quest::AuthoredNode& hero = quest->prerequisites.back();
    hero.hero_requirement = Quest::HeroRequirementKind::Renown;
    hero.numeric_comparison = Quest::NumericComparison::AtLeast;
    hero.hero_value = "1000";
    refresh_authored_lua();
}

void SetPrerequisiteInspectorCaptureScrollBottom(bool enabled) {
    g_prerequisite_capture_scroll_bottom = enabled;
    g_prerequisite_capture_scroll_fraction = enabled ? 1.0f : -1.0f;
}

void SetPrerequisiteInspectorCaptureScrollFraction(float fraction) {
    g_prerequisite_capture_scroll_bottom = false;
    g_prerequisite_capture_scroll_fraction =
        std::clamp(fraction, 0.0f, 1.0f);
}

LevelReferenceTarget PendingLevelReferenceTarget() {
    return g_level_reference_target;
}

std::string PendingLevelReferenceLabel() {
    if (BlueprintUI::PendingPickPin() != 0) {
        return "blueprint pin reference";
    }
    const Quest::AuthoredQuest* quest = active_authored_quest_const();
    const Quest::AuthoredNode* node = quest
        ? Quest::FindAuthoredNode(*quest, g_level_reference_node) : nullptr;
    if (!node) return "quest node reference";
    if (g_level_reference_target == LevelReferenceTarget::RequiredItemSource) {
        return "item source for " +
               std::string(Quest::AuthoredNodeKindName(node->kind));
    }
    return "NPC for " +
           std::string(Quest::AuthoredNodeKindName(node->kind));
}

uint32_t PendingLevelReferenceItemHash() {
    const Quest::AuthoredQuest* quest = active_authored_quest_const();
    const Quest::AuthoredNode* node = quest
        ? Quest::FindAuthoredNode(*quest, g_level_reference_node) : nullptr;
    return node &&
           g_level_reference_target == LevelReferenceTarget::RequiredItemSource
        ? node->item.record_hash : 0;
}

bool BindActiveLevelReference(const LevelReferenceCandidate& candidate,
                              std::string& error) {
    error.clear();
    if (BlueprintUI::PendingPickPin() != 0) {
        return BlueprintUI::BindPendingPin(
            candidate.level_path, candidate.level_id, candidate.entity_name,
            candidate.entity_hash, candidate.x, candidate.y, candidate.z,
            candidate.model_hashes, candidate.authored_instance, error);
    }
    Quest::AuthoredQuest* quest = active_authored_quest();
    Quest::AuthoredNode* node = quest
        ? Quest::FindAuthoredNode(*quest, g_level_reference_node) : nullptr;
    if (!node) {
        error = "Select a quest node and choose its level reference first.";
        return false;
    }
    Quest::WorldReference reference;
    reference.level_path = candidate.level_path;
    reference.level_id = candidate.level_id;
    reference.entity_name = candidate.entity_name;
    reference.entity_hash = candidate.entity_hash;
    reference.x = candidate.x;
    reference.y = candidate.y;
    reference.z = candidate.z;
    reference.model_hashes = candidate.model_hashes;
    reference.authored_instance = candidate.authored_instance;
    if (!reference.valid()) {
        error = "The selected level entity has no usable GDB name.";
        return false;
    }
    if (g_level_reference_target == LevelReferenceTarget::QuestGiver) {
        if (!candidate.is_npc) {
            error = "Right-click an NPC marker for this node.";
            return false;
        }
        node->entity = std::move(reference);
    } else {
        if (!candidate.is_container) {
            error = "Right-click a container for this item source.";
            return false;
        }
        if (!node->item.valid()) {
            error = "Choose the item in this node first.";
            return false;
        }
        node->item.source = std::move(reference);
    }
    refresh_authored_lua();
    return true;
}

bool GetPendingNpcCreation(NpcCreationRequest& request) {
    if (g_pending_npc_creation.creature_entity == 0 ||
        g_pending_npc_node == 0 || g_pending_npc_quest_id.empty()) {
        request = NpcCreationRequest{};
        return false;
    }
    request = g_pending_npc_creation;
    return true;
}

bool BindCreatedNpcInstance(const LevelReferenceCandidate& candidate,
                            std::string& error) {
    error.clear();
    if (!candidate.is_npc || !candidate.authored_instance) {
        error = "The created quest NPC reference is invalid.";
        return false;
    }
    Quest::AuthoredQuest* quest = nullptr;
    for (Quest::AuthoredQuest& authored : g_authored_quests) {
        if (authored.quest_id == g_pending_npc_quest_id) {
            quest = &authored;
            break;
        }
    }
    Quest::AuthoredNode* node = quest
        ? Quest::FindAuthoredNode(*quest, g_pending_npc_node) : nullptr;
    if (!quest || !node) {
        error = "The quest NPC's source node is no longer available.";
        return false;
    }
    Quest::WorldReference reference;
    reference.level_path = candidate.level_path;
    reference.level_id = candidate.level_id;
    reference.entity_name = candidate.entity_name;
    reference.entity_hash = candidate.entity_hash;
    reference.x = candidate.x;
    reference.y = candidate.y;
    reference.z = candidate.z;
    reference.model_hashes = candidate.model_hashes;
    reference.authored_instance = true;
    if (!reference.valid()) {
        error = "The created NPC has no usable level reference.";
        return false;
    }
    node->entity = std::move(reference);
    CancelPendingNpcCreation();
    refresh_authored_lua();
    return true;
}

void CancelPendingNpcCreation() {
    g_pending_npc_creation = NpcCreationRequest{};
    g_pending_npc_quest_id.clear();
    g_pending_npc_node = 0;
}

const char* authored_prerequisite_title(Quest::AuthoredNodeKind kind) {
    switch (kind) {
        case Quest::AuthoredNodeKind::PrerequisiteStoryProgress:
            return "Story progression";
        case Quest::AuthoredNodeKind::PrerequisiteQuestState:
            return "Quest state";
        case Quest::AuthoredNodeKind::PrerequisiteGameflowFlag:
            return "Named gameflow condition";
        case Quest::AuthoredNodeKind::PrerequisiteLuaCondition:
            return "Lua condition";
        case Quest::AuthoredNodeKind::PrerequisiteHeroRequirement:
            return "Hero requirement";
        default:
            return "Prerequisite";
    }
}

bool draw_required_quest_state(const char* id,
                               Quest::RequiredQuestState& state) {
    bool changed = false;
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo(id, Quest::RequiredQuestStateName(state))) {
        for (Quest::RequiredQuestState candidate : {
                 Quest::RequiredQuestState::Registered,
                 Quest::RequiredQuestState::Activated,
                 Quest::RequiredQuestState::Active,
                 Quest::RequiredQuestState::Completed,
                 Quest::RequiredQuestState::Unlocked}) {
            const bool selected = state == candidate;
            if (ImGui::Selectable(
                    Quest::RequiredQuestStateName(candidate), selected)) {
                state = candidate;
                changed = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool draw_yes_no_combo(const char* id, bool& value) {
    bool changed = false;
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo(id, value ? "Yes" : "No")) {
        if (ImGui::Selectable("Yes", value)) {
            value = true;
            changed = true;
        }
        if (ImGui::Selectable("No", !value)) {
            value = false;
            changed = true;
        }
        ImGui::EndCombo();
    }
    return changed;
}

struct NamedGameflowCondition {
    const char* label;
    const char* variable;
};

constexpr NamedGameflowCondition kNamedGameflowConditions[] = {
    {"Westcliff investment opportunity missed", "OpportunityMissed"},
    {"Thag camp gypsy-prisoner sequence required", "GypsiesNeeded"},
    {"Banshee active in Bloodstone", "BansheeInBloodstone"},
    {"Temple of Evil destroyed", "TempleOfEvilDestroyed"},
    {"Brightwood farmer quest completed", "BrightwoodFarmerComplete"},
};

const char* named_gameflow_label(const std::string& variable) {
    for (const NamedGameflowCondition& condition :
         kNamedGameflowConditions) {
        if (variable == condition.variable) return condition.label;
    }
    return variable.empty() ? "Select condition..." : variable.c_str();
}

bool draw_named_gameflow_condition(Quest::AuthoredNode& prerequisite) {
    bool changed = false;
    ImGui::TextUnformatted("Condition");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##condition",
                          named_gameflow_label(
                              prerequisite.gameflow_flag))) {
        for (const NamedGameflowCondition& condition :
             kNamedGameflowConditions) {
            const bool selected =
                prerequisite.gameflow_flag == condition.variable;
            if (ImGui::Selectable(condition.label, selected)) {
                prerequisite.gameflow_flag = condition.variable;
                changed = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::TextUnformatted("Required value");
    changed |= draw_yes_no_combo("##required_value", prerequisite.expected);
    return changed;
}

std::string hero_ability_label(const std::string& value) {
    constexpr std::string_view prefix = "EHeroAbilityType.HERO_ABILITY_";
    std::string label = value;
    if (label.rfind(prefix, 0) == 0) label.erase(0, prefix.size());
    std::replace(label.begin(), label.end(), '_', ' ');
    std::transform(label.begin(), label.end(), label.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    if (!label.empty()) label.front() =
        static_cast<char>(std::toupper(
            static_cast<unsigned char>(label.front())));
    return label.empty() ? "Select ability..." : label;
}

bool draw_numeric_comparison(Quest::NumericComparison& comparison) {
    bool changed = false;
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##comparison",
                          Quest::NumericComparisonName(comparison))) {
        for (Quest::NumericComparison candidate : {
                 Quest::NumericComparison::AtLeast,
                 Quest::NumericComparison::AtMost,
                 Quest::NumericComparison::Exactly}) {
            const bool selected = candidate == comparison;
            if (ImGui::Selectable(
                    Quest::NumericComparisonName(candidate), selected)) {
                comparison = candidate;
                changed = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool draw_hero_requirement(Quest::AuthoredNode& prerequisite) {
    bool changed = false;
    ImGui::TextUnformatted("Requirement");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo(
            "##hero_requirement",
            Quest::HeroRequirementKindName(prerequisite.hero_requirement))) {
        for (Quest::HeroRequirementKind candidate : {
                 Quest::HeroRequirementKind::Renown,
                 Quest::HeroRequirementKind::Alignment,
                 Quest::HeroRequirementKind::Purity,
                 Quest::HeroRequirementKind::Gold,
                 Quest::HeroRequirementKind::AbilityLevel,
                 Quest::HeroRequirementKind::Age,
                 Quest::HeroRequirementKind::Gender,
                 Quest::HeroRequirementKind::Married,
                 Quest::HeroRequirementKind::SpouseCount,
                 Quest::HeroRequirementKind::HasChildren}) {
            const bool selected = candidate == prerequisite.hero_requirement;
            if (ImGui::Selectable(
                    Quest::HeroRequirementKindName(candidate), selected)) {
                prerequisite.hero_requirement = candidate;
                prerequisite.expected = true;
                if (candidate == Quest::HeroRequirementKind::Gender) {
                    prerequisite.hero_option = "Male";
                } else if (candidate ==
                           Quest::HeroRequirementKind::AbilityLevel) {
                    const auto& abilities = Quest::HeroAbilityTypes();
                    if (std::find(abilities.begin(), abilities.end(),
                                  prerequisite.hero_option) ==
                        abilities.end()) {
                        prerequisite.hero_option = abilities.front();
                    }
                }
                changed = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    if (prerequisite.hero_requirement ==
        Quest::HeroRequirementKind::Gender) {
        ImGui::TextUnformatted("Gender");
        ImGui::SetNextItemWidth(-1.0f);
        const char* current = prerequisite.hero_option == "Female"
            ? "Female" : "Male";
        if (ImGui::BeginCombo("##gender", current)) {
            for (const char* candidate : {"Male", "Female"}) {
                const bool selected = prerequisite.hero_option == candidate;
                if (ImGui::Selectable(candidate, selected)) {
                    prerequisite.hero_option = candidate;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        return changed;
    }
    if (prerequisite.hero_requirement ==
            Quest::HeroRequirementKind::Married ||
        prerequisite.hero_requirement ==
            Quest::HeroRequirementKind::HasChildren) {
        ImGui::TextUnformatted("Required value");
        changed |= draw_yes_no_combo("##hero_bool", prerequisite.expected);
        return changed;
    }

    if (prerequisite.hero_requirement ==
        Quest::HeroRequirementKind::AbilityLevel) {
        ImGui::TextUnformatted("Ability");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo(
                "##ability",
                hero_ability_label(prerequisite.hero_option).c_str())) {
            for (const std::string& ability : Quest::HeroAbilityTypes()) {
                const bool selected = prerequisite.hero_option == ability;
                const std::string label = hero_ability_label(ability);
                if (ImGui::Selectable(label.c_str(), selected)) {
                    prerequisite.hero_option = ability;
                    changed = true;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }

    ImGui::TextUnformatted("Comparison");
    changed |= draw_numeric_comparison(prerequisite.numeric_comparison);
    ImGui::TextUnformatted("Value");
    ImGui::SetNextItemWidth(-1.0f);
    changed |= ImGui::InputText("##value", &prerequisite.hero_value);
    int parsed = 0;
    if (!Quest::ParseAuthoredInteger(prerequisite.hero_value, parsed)) {
        ImGui::TextColored(ImVec4(1.0f, 0.36f, 0.32f, 1.0f),
                           "Enter a whole number.");
    }
    return changed;
}

std::string quest_npc_role(const Quest::AuthoredNode& node) {
    switch (node.kind) {
        case Quest::AuthoredNodeKind::ApproachNpc: return "QuestGiver";
        case Quest::AuthoredNodeKind::Dialogue: return "Speaker";
        case Quest::AuthoredNodeKind::ReturnToNpc: return "ReturnNpc";
        default: return "Npc";
    }
}

std::string unique_quest_npc_name(const Quest::AuthoredQuest& quest,
                                  const Quest::AuthoredNode& node) {
    const std::string base = quest.quest_id + "_" + quest_npc_role(node);
    auto used = [&](const std::string& name) {
        if (g_pending_npc_creation.instance_name == name) return true;
        for (const Quest::AuthoredQuest& authored : g_authored_quests) {
            for (const Quest::AuthoredNode& authored_node : authored.nodes) {
                if (authored_node.entity.entity_name == name) return true;
            }
        }
        for (const LevelSpawnMarker& marker : g_level_spawn_markers) {
            if (marker.name == name) return true;
        }
        return false;
    };
    if (!used(base)) return base;
    for (int suffix = 2; suffix < 10000; ++suffix) {
        const std::string candidate =
            base + "_" + std::to_string(suffix);
        if (!used(candidate)) return candidate;
    }
    return base + "_New";
}

void draw_create_npc_picker(Quest::AuthoredQuest& quest,
                            Quest::AuthoredNode& node) {
    if (ImGui::Button("Place new NPC instance...", ImVec2(-1.0f, 0.0f))) {
        g_npc_creation_filter[0] = 0;
        ImGui::OpenPopup("Create quest NPC instance");
    }
    ImGui::SetNextWindowSize(ImVec2(460.0f, 520.0f),
                             ImGuiCond_Appearing);
    if (ImGui::BeginPopup("Create quest NPC instance")) {
        ImGui::TextUnformatted("Choose the NPC definition");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##npc_definition_search",
                                 "Search entities...",
                                 g_npc_creation_filter,
                                 sizeof(g_npc_creation_filter));
        std::string filter = g_npc_creation_filter;
        std::transform(filter.begin(), filter.end(), filter.begin(),
                       [](unsigned char c) {
                           return static_cast<char>(std::tolower(c));
                       });
        std::vector<std::size_t> matches;
        matches.reserve(g_global_entity_catalog.size());
        for (std::size_t index = 0;
             index < g_global_entity_catalog.size(); ++index) {
            const Gdb::CreatureCatalogEntry& entity =
                g_global_entity_catalog[index];
            if (entity.kind != Gdb::EntityCatalogKind::Creature) continue;
            std::string searchable = entity.display_name + " " + entity.name;
            std::transform(searchable.begin(), searchable.end(),
                           searchable.begin(), [](unsigned char c) {
                               return static_cast<char>(std::tolower(c));
                           });
            if (filter.empty() ||
                searchable.find(filter) != std::string::npos) {
                matches.push_back(index);
            }
        }
        ImGui::Separator();
        ImGui::BeginChild("##npc_definition_results", ImVec2(0.0f, 0.0f));
        if (matches.empty()) {
            ImGui::TextDisabled("No matching NPC definitions.");
        } else {
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(matches.size()));
            while (clipper.Step()) {
                for (int row = clipper.DisplayStart;
                     row < clipper.DisplayEnd; ++row) {
                    const Gdb::CreatureCatalogEntry& entity =
                        g_global_entity_catalog[matches[
                            static_cast<std::size_t>(row)]];
                    const std::string label = entity.display_name.empty()
                        ? entity.name : entity.display_name;
                    ImGui::PushID(static_cast<int>(entity.entity_hash));
                    if (ImGui::Selectable(label.c_str())) {
                        g_pending_npc_creation = NpcCreationRequest{};
                        g_pending_npc_creation.instance_name =
                            unique_quest_npc_name(quest, node);
                        g_pending_npc_creation.creature_name = entity.name;
                        g_pending_npc_creation.display_name = label;
                        g_pending_npc_creation.creature_entity =
                            entity.entity_hash;
                        g_pending_npc_creation.model_hashes =
                            entity.model_hashes;
                        g_pending_npc_quest_id = quest.quest_id;
                        g_pending_npc_node = node.id;
                        g_level_reference_target =
                            LevelReferenceTarget::QuestGiver;
                        g_level_reference_node = node.id;
                        ImGui::CloseCurrentPopup();
                    }
                    if (ImGui::IsItemHovered() && label != entity.name) {
                        ImGui::SetTooltip("%s", entity.name.c_str());
                    }
                    ImGui::PopID();
                }
            }
            clipper.End();
        }
        ImGui::EndChild();
        ImGui::EndPopup();
    }
    if (g_pending_npc_node == node.id &&
        g_pending_npc_quest_id == quest.quest_id &&
        g_pending_npc_creation.creature_entity != 0) {
        ImGui::TextWrapped("Ready to place: %s",
                           g_pending_npc_creation.display_name.c_str());
        ImGui::TextDisabled("Right-click its position in an editable level.");
        if (ImGui::Button("Cancel NPC placement", ImVec2(-1.0f, 0.0f))) {
            CancelPendingNpcCreation();
        }
    }
}

std::string current_quest_level_id(std::string path) {
    std::replace(path.begin(), path.end(), '/', '\\');
    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    const std::string root = "worlds\\albion\\";
    const std::size_t root_pos = lower.find(root);
    if (root_pos != std::string::npos) {
        path.erase(0, root_pos + root.size());
        lower.erase(0, root_pos + root.size());
    }
    const std::string suffix =
        "\\defaultscenario\\defaultscenario.engine_level";
    const std::size_t suffix_pos = lower.rfind(suffix);
    if (suffix_pos != std::string::npos) path.resize(suffix_pos);
    if (path.empty()) path = g_pending_terrain_label;
    return path;
}

struct PlacedNpcChoice {
    Quest::WorldReference reference;
    std::string label;
    std::string searchable;
};

std::vector<PlacedNpcChoice> current_level_npc_instances() {
    std::vector<PlacedNpcChoice> choices;
    const std::string level_path =
        g_pending_terrain_level_entry.full_path;
    const std::string level_id = current_quest_level_id(level_path);
    if (level_id.empty()) return choices;

    choices.reserve(g_level_spawn_markers.size());
    for (std::size_t marker_index = 0;
         marker_index < g_level_spawn_markers.size(); ++marker_index) {
        const LevelSpawnMarker& marker =
            g_level_spawn_markers[marker_index];
        if ((marker.kind != 3 && marker.kind != 6) ||
            marker.name.empty() ||
            (marker.entity_hash != 0 &&
             LevelEdit::EntityRemovalPending(marker.entity_hash))) {
            continue;
        }

        Quest::WorldReference reference;
        reference.level_path = level_path;
        reference.level_id = level_id;
        reference.entity_name = marker.name;
        reference.entity_hash = marker.entity_hash;
        reference.x = marker.x;
        reference.y = marker.y;
        reference.z = marker.z;
        float position_delta[3]{};
        float rotation_delta[3]{};
        if (LevelEdit::EditFor(
                0x70000000u | static_cast<uint32_t>(marker_index),
                position_delta, rotation_delta)) {
            reference.x += position_delta[0];
            reference.y += position_delta[1];
            reference.z += position_delta[2];
        }
        reference.model_hashes = marker.model_hashes;
        reference.authored_instance =
            marker.pending_addition_index >= 0 &&
            LevelEdit::AdditionIsNamedEntity(
                marker.pending_addition_index);
        if (!reference.valid()) continue;

        char coords[128]{};
        std::snprintf(coords, sizeof(coords),
                      "  |  %s  |  (%.2f, %.2f, %.2f)",
                      level_id.c_str(), reference.x, reference.y,
                      reference.z);
        PlacedNpcChoice choice;
        choice.reference = std::move(reference);
        choice.label = marker.name;
        if (!marker.creature_name.empty() &&
            marker.creature_name != marker.name) {
            choice.label += "  [" + marker.creature_name + "]";
        }
        choice.label += coords;
        choice.searchable = choice.label;
        std::transform(choice.searchable.begin(), choice.searchable.end(),
                       choice.searchable.begin(), [](unsigned char c) {
                           return static_cast<char>(std::tolower(c));
                       });
        choices.push_back(std::move(choice));
    }
    std::sort(choices.begin(), choices.end(),
              [](const PlacedNpcChoice& a, const PlacedNpcChoice& b) {
                  if (a.reference.entity_name != b.reference.entity_name) {
                      return a.reference.entity_name < b.reference.entity_name;
                  }
                  if (a.reference.x != b.reference.x) {
                      return a.reference.x < b.reference.x;
                  }
                  if (a.reference.y != b.reference.y) {
                      return a.reference.y < b.reference.y;
                  }
                  return a.reference.z < b.reference.z;
              });
    return choices;
}

bool draw_npc_instance_picker(Quest::AuthoredNode& node) {
    const std::string preview = node.entity.valid()
        ? node.entity.entity_name + "  |  " + node.entity.level_id
        : std::string("Select named entity...");
    bool changed = false;
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##npc_instance", preview.c_str(),
                          ImGuiComboFlags_HeightLarge)) {
        if (ImGui::IsWindowAppearing()) g_npc_instance_filter[0] = 0;
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##npc_instance_search",
                                 "Search placed named entities...",
                                 g_npc_instance_filter,
                                 sizeof(g_npc_instance_filter));
        std::string filter = g_npc_instance_filter;
        std::transform(filter.begin(), filter.end(), filter.begin(),
                       [](unsigned char c) {
                           return static_cast<char>(std::tolower(c));
                       });
        const std::vector<PlacedNpcChoice> choices =
            current_level_npc_instances();
        ImGui::Separator();
        if (choices.empty()) {
            ImGui::TextDisabled(
                "No named NPC or static-prop instances in the loaded level.");
        } else {
            ImGui::BeginChild("##npc_instance_results",
                              ImVec2(0.0f, 260.0f), false);
            for (std::size_t index = 0; index < choices.size(); ++index) {
                const PlacedNpcChoice& choice = choices[index];
                if (!filter.empty() &&
                    choice.searchable.find(filter) == std::string::npos) {
                    continue;
                }
                ImGui::PushID(static_cast<int>(index));
                const bool selected =
                    node.entity.entity_name == choice.reference.entity_name &&
                    node.entity.level_id == choice.reference.level_id &&
                    std::abs(node.entity.x - choice.reference.x) < 0.001 &&
                    std::abs(node.entity.y - choice.reference.y) < 0.001 &&
                    std::abs(node.entity.z - choice.reference.z) < 0.001;
                if (ImGui::Selectable(choice.label.c_str(), selected)) {
                    node.entity = choice.reference;
                    CancelPendingNpcCreation();
                    changed = true;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
        }
        ImGui::EndCombo();
    }
    return changed;
}

const Quest::GraphNode* find_graph_node(const Quest::Graph& graph, int id) {
    for (const Quest::GraphNode& node : graph.nodes) {
        if (node.id == id) return &node;
    }
    return nullptr;
}

void draw_read_only_field(const char* label, const std::string& value) {
    ImGui::TextUnformatted(label);
    std::string copy = value;
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::BeginDisabled();
    ImGui::InputText((std::string("##") + label).c_str(), &copy,
                     ImGuiInputTextFlags_ReadOnly);
    ImGui::EndDisabled();
}

std::vector<std::string> read_only_prerequisites(
    const Quest::Graph& graph, const Quest::GraphNode& start) {
    std::vector<std::string> prerequisites;
    auto append = [&](const std::string& value) {
        if (value.empty() ||
            std::find(prerequisites.begin(), prerequisites.end(), value) !=
                prerequisites.end()) return;
        prerequisites.push_back(value);
    };

    for (const Quest::QuestEvent& event : start.events) {
        if (event.kind == Quest::QuestEventKind::Condition) {
            append(event.condition.empty() ? event.title : event.condition);
        }
    }
    for (const std::string& detail : start.details) {
        if (contains_case_insensitive(detail, "prerequisite") ||
            contains_case_insensitive(detail, "available after") ||
            contains_case_insensitive(detail, "unavailable after")) {
            append(detail);
        }
    }




    for (const Quest::GraphLink& link : graph.links) {
        if (link.to_node != start.id) continue;
        const Quest::GraphNode* source = find_graph_node(graph, link.from_node);
        if (!source) continue;
        bool is_condition = source->kind == Quest::NodeKind::State;
        for (const Quest::QuestEvent& event : source->events) {
            is_condition |= event.kind == Quest::QuestEventKind::Condition;
        }
        if (is_condition) append(source->title);
    }
    return prerequisites;
}

bool read_only_completion_node(const Quest::GraphNode& node) {
    for (const Quest::QuestEvent& event : node.events) {
        if (event.kind == Quest::QuestEventKind::QuestComplete) return true;
        if (event.kind == Quest::QuestEventKind::QuestFail) return false;
    }
    if (contains_case_insensitive(node.badge, "failed") ||
        contains_case_insensitive(node.title, "failed")) return false;
    if (contains_case_insensitive(node.badge, "ending choice")) return false;
    if (contains_case_insensitive(node.badge, "ending") ||
        contains_case_insensitive(node.badge, "quest end")) return true;
    return std::any_of(node.details.begin(), node.details.end(),
                       [](const std::string& detail) {
                           return contains_case_insensitive(
                               detail, "quest complete");
                       });
}

bool reward_event(const Quest::QuestEvent& event) {
    return event.kind == Quest::QuestEventKind::Reward ||
           event.kind == Quest::QuestEventKind::Morality ||
           event.kind == Quest::QuestEventKind::InventoryAdd;
}

std::string read_only_reward_text(const Quest::QuestEvent& event) {
    std::string label;
    if (event.kind == Quest::QuestEventKind::Morality) {
        label = "Morality";
    } else if (event.kind == Quest::QuestEventKind::InventoryAdd) {
        label = event.item.empty() ? "Item" : event.item;
        std::replace(label.begin(), label.end(), '_', ' ');
        return "Item: " + label;
    } else {
        label = event.item.empty() ? event.title : event.item;
        if (label.empty()) label = "Reward";
    }
    if (!event.amount) return label;
    const double value = *event.amount;
    const long long whole = static_cast<long long>(value);
    std::string amount = value == static_cast<double>(whole)
        ? std::to_string(whole) : std::to_string(value);
    if ((label == "Morality" || label == "Purity") && value > 0.0) {
        amount.insert(amount.begin(), '+');
    }
    return label + ": " + amount;
}

std::vector<std::string> read_only_completion_rewards(
    const Quest::Graph& graph, const Quest::GraphNode& completion) {
    std::vector<std::string> rewards;
    std::unordered_set<std::string> seen;
    auto append = [&](const std::string& value) {
        if (!value.empty() && seen.insert(value).second) {
            rewards.push_back(value);
        }
    };
    for (const Quest::QuestEvent& event : completion.events) {
        if (reward_event(event)) append(read_only_reward_text(event));
    }




    for (const Quest::QuestEvent& completed : completion.events) {
        if (completed.kind != Quest::QuestEventKind::QuestComplete ||
            completed.source_method.empty()) continue;
        for (const Quest::GraphNode& candidate_node : graph.nodes) {
            for (const Quest::QuestEvent& candidate : candidate_node.events) {
                if (!reward_event(candidate) ||
                    candidate.source_method != completed.source_method ||
                    candidate.source_class != completed.source_class) {
                    continue;
                }
                const std::size_t earlier =
                    candidate.source_line < completed.source_line
                        ? completed.source_line - candidate.source_line
                        : candidate.source_line - completed.source_line;
                if (earlier <= 100) append(read_only_reward_text(candidate));
            }
        }
    }



    for (const std::string& detail : completion.details) {
        if (contains_case_insensitive(detail, "morality:") ||
            contains_case_insensitive(detail, "purity:") ||
            contains_case_insensitive(detail, "reward:")) {
            append(detail);
        }
    }
    return rewards;
}

void DrawSelectedReadOnlyNodeInspector(const Quest::Graph& graph,
                                       const Quest::GraphNode& node) {
    ImGui::TextColored(ImVec4(0.55f, 0.78f, 1.0f, 1.0f), "NODE");
    ImGui::TextWrapped("%s", node.badge.empty()
                                ? Quest::NodeKindName(node.kind)
                                : node.badge.c_str());
    ImGui::Separator();
    ImGui::TextDisabled("Read-only original quest");

    const bool quest_completion = read_only_completion_node(node);
    const bool quest_start = !quest_completion &&
        (node.kind == Quest::NodeKind::Quest ||
         contains_case_insensitive(node.badge, "quest start"));
    if (quest_start) {
        const std::string& name = node.title.empty() ? graph.title : node.title;
        draw_read_only_field("Quest name", name);
        ImGui::Spacing();
        ImGui::SeparatorText("Prerequisites");
        const std::vector<std::string> prerequisites =
            read_only_prerequisites(graph, node);
        if (prerequisites.empty()) {
            ImGui::TextDisabled("None found in this quest script");
        } else {
            for (const std::string& prerequisite : prerequisites) {
                ImGui::BulletText("%s", prerequisite.c_str());
            }
        }
        return;
    }

    if (quest_completion) {
        draw_read_only_field("Title", node.title);
        if (!node.subtitle.empty()) {
            draw_read_only_field("Context", node.subtitle);
        }
        ImGui::Spacing();
        ImGui::SeparatorText("Rewards");
        const std::vector<std::string> rewards =
            read_only_completion_rewards(graph, node);
        if (rewards.empty()) {
            ImGui::TextDisabled("None found");
        } else {
            for (const std::string& reward : rewards) {
                ImGui::BulletText("%s", reward.c_str());
            }
        }
        if (!node.details.empty()) {
            ImGui::Spacing();
            ImGui::SeparatorText("Details");
            for (const std::string& detail : node.details) {
                ImGui::TextWrapped("%s", detail.c_str());
            }
        }
        return;
    }

    draw_read_only_field("Title", node.title);
    if (!node.subtitle.empty()) {
        draw_read_only_field("Context", node.subtitle);
    }
    if (!node.details.empty()) {
        ImGui::Spacing();
        ImGui::SeparatorText("Details");
        for (const std::string& detail : node.details) {
            ImGui::TextWrapped("%s", detail.c_str());
        }
    }
}

void RefreshReferenceCatalog() {
    {
        std::lock_guard<std::mutex> lock(g_graph_mutex);
        if (g_reference_ready && g_reference_root == S.root_dir &&
            g_audio_assets.size() == S.all_wav_files.size() &&
            g_level_assets.size() == S.all_level_files.size()) return;
    }
    std::vector<std::string> audio;
    std::unordered_map<std::string, std::vector<std::string>> audio_by_dialogue;
    std::vector<std::string> levels;
    audio.reserve(S.all_wav_files.size());
    levels.reserve(S.all_level_files.size());
    for (const FlatAssetEntry& entry : S.all_wav_files) {
        const std::string path = entry.full_path.empty() ? entry.name : entry.full_path;
        audio.push_back(path);
        std::string stem = entry.name.empty() ? path : entry.name;
        const std::size_t slash = stem.find_last_of("/\\");
        if (slash != std::string::npos) stem.erase(0, slash + 1);
        const std::size_t dot = stem.find_last_of('.');
        if (dot != std::string::npos) stem.resize(dot);
        std::transform(stem.begin(), stem.end(), stem.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });
        audio_by_dialogue[stem].push_back(path);
    }
    for (const FlatAssetEntry& entry : S.all_level_files) {
        levels.push_back(entry.full_path.empty() ? entry.name : entry.full_path);
    }
    auto cutscene_database = load_cutscene_database(S.root_dir);

    std::lock_guard<std::mutex> lock(g_graph_mutex);
    const bool reset_world_index = g_reference_root != S.root_dir ||
                                   g_level_assets.size() != levels.size();
    g_reference_root = S.root_dir;
    g_audio_assets = std::move(audio);
    g_audio_by_dialogue = std::move(audio_by_dialogue);
    g_level_assets = std::move(levels);
    if (reset_world_index) {
        g_world_entities.clear();
        g_world_queries.clear();
    }
    g_cutscene_database = std::move(cutscene_database);
    g_reference_ready = true;
}

void SetQuestSource(const std::string& title,
                    const std::string& decompiled_lua) {
    g_active_authored_quest = -1;
    BlueprintUI::CloseActive();
    Quest::ReferenceCatalog references;
    std::string root;
    std::shared_ptr<const std::vector<uint8_t>> cutscene_database;
    std::vector<std::string> missing_world_names;
    const std::vector<std::string> world_names =
        Quest::FindWorldReferenceNames(decompiled_lua);
    {
        std::lock_guard<std::mutex> lock(g_graph_mutex);
        root = g_reference_root;
        references.audio_assets = g_audio_assets;
        references.audio_by_dialogue = g_audio_by_dialogue;
        references.level_assets = g_level_assets;
        references.world_entities = g_world_entities;
        for (const std::string& name : world_names) {
            if (g_world_queries.insert(name).second) {
                missing_world_names.push_back(name);
            }
        }
        cutscene_database = g_cutscene_database;
    }

    if (!missing_world_names.empty()) {
        std::vector<Quest::WorldIndexAsset> level_assets;
        level_assets.reserve(S.all_level_files.size());
        for (const FlatAssetEntry& entry : S.all_level_files) {
            level_assets.push_back({entry.name, entry.full_path, entry.bnk_path,
                                    entry.file_index});
        }
        auto found = Quest::IndexWorldPlacements(level_assets,
                                                 missing_world_names);
        std::lock_guard<std::mutex> lock(g_graph_mutex);
        for (auto& entry : found) {
            std::vector<Quest::WorldEntityPlacement>& placements =
                g_world_entities[entry.first];
            placements.insert(placements.end(), entry.second.begin(),
                              entry.second.end());
        }
        references.world_entities = g_world_entities;
    }

    if (!root.empty()) {
        TextBank::LoadForRoot(root);
        const std::filesystem::path globals =
            std::filesystem::path(root) / "data" / "Globals" /
            "globals.gdb";
        references.quest_rewards = Quest::ExtractQuestRecordRewards(
            globals.string(), decompiled_lua);
        for (Quest::QuestRewardReference& reward :
             references.quest_rewards) {
            if (reward.item_text_tag.empty()) continue;
            std::string display_name;
            if (TextBank::LookupTag(reward.item_text_tag, display_name) &&
                !display_name.empty()) {
                reward.label = std::move(display_name);
            }
        }
    }
    if (cutscene_database) {
        references.cutscenes = Quest::ExtractCutsceneReferences(
            *cutscene_database, Quest::FindCutsceneIds(decompiled_lua));
    }
    for (const std::string& tag :
         Quest::FindLuaStringLiterals(decompiled_lua)) {
        if (references.localized_text.count(tag)) continue;
        std::string resolved;
        if (TextBank::LookupTag(tag, resolved)) {
            references.localized_text.emplace(tag, std::move(resolved));
        }
    }
    for (const auto& cutscene : references.cutscenes) {
        for (const std::string& tag : cutscene.second.dialogue_tags) {
            if (references.localized_text.count(tag)) continue;
            std::string resolved;
            if (TextBank::LookupTag(tag, resolved)) {
                references.localized_text.emplace(tag, std::move(resolved));
            }
        }
    }

    auto graph = std::make_shared<Quest::Graph>(
        Quest::BuildStoryGraph(title, decompiled_lua, references));
    std::lock_guard<std::mutex> lock(g_graph_mutex);
    g_graph = std::move(graph);
    ++g_graph_generation;
}

void SetInitialViewAll(bool enabled) {
    g_initial_view_all = enabled;
}

void SetInitialFocusNodeCount(std::size_t count) {
    g_initial_focus_node_count = std::max<std::size_t>(1, count);
    g_initial_focus_node_start = 0;
    g_initial_focus_explicit_range = false;
}

void SetInitialFocusNodeRange(std::size_t start, std::size_t count) {
    g_initial_focus_node_start = start;
    g_initial_focus_node_count = std::max<std::size_t>(1, count);
    g_initial_focus_explicit_range = true;
}

void Clear() {
    g_active_authored_quest = -1;
    BlueprintUI::CloseActive();
    CancelPendingNpcCreation();
    std::lock_guard<std::mutex> lock(g_graph_mutex);
    g_graph.reset();
    ++g_graph_generation;
}

void DrawNodeView() {
    if (BlueprintUI::IsActive()) {
        BlueprintUI::Draw();
        return;
    }
    std::shared_ptr<const Quest::Graph> graph;
    std::uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(g_graph_mutex);
        graph = g_graph;
        generation = g_graph_generation;
    }

    if (!graph) {
        ImGui::TextDisabled("Building quest graph...");
        return;
    }
    if (graph->nodes.empty()) {
        ImGui::TextDisabled("No quest threads or functions were found in this script.");
        return;
    }

    if (generation != g_visible_generation) {
        g_visible_generation = generation;
        reset_editor();
    } else {
        ensure_editor();
    }

    const Quest::GraphNode* selected_node =
        find_graph_node(*graph, g_selected_graph_node);
    const bool show_inspector = selected_node != nullptr;
    if (show_inspector) {
        constexpr float inspector_width = 360.0f;
        const float inspector_height =
            ImGui::GetContentRegionAvail().y * 0.5f;
        ImGui::BeginChild("##read_only_node_inspector",
                          ImVec2(inspector_width, inspector_height), true);
        DrawSelectedReadOnlyNodeInspector(*graph, *selected_node);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##read_only_node_canvas", ImVec2(0.0f, 0.0f),
                          false);
    }

    ed::SetCurrentEditor(g_editor);
    ed::Begin("quest_node_canvas", ImGui::GetContentRegionAvail());

    for (const Quest::GraphNode& node : graph->nodes) {
        draw_node(node);
    }

    struct LinkLabel {
        const Quest::GraphNode* source = nullptr;
        const Quest::GraphNode* target = nullptr;
        std::string text;
        ImVec4 color;
    };
    std::vector<LinkLabel> link_labels;
    for (const Quest::GraphLink& link : graph->links) {
        const Quest::GraphNode* source = nullptr;
        const Quest::GraphNode* target = nullptr;
        for (const Quest::GraphNode& node : graph->nodes) {
            if (node.id == link.from_node) source = &node;
            if (node.id == link.to_node) {
                target = &node;
            }
        }
        ImVec4 color = link_color(target);
        if (link.label == "Yes") color = ImVec4(0.38f, 0.86f, 0.48f, 0.95f);
        if (link.label == "No") color = ImVec4(0.95f, 0.38f, 0.36f, 0.95f);
        if (link.label == "any order") {
            color = ImVec4(0.93f, 0.72f, 0.30f, 0.95f);
        }
        if (link.inferred) color = ImVec4(0.55f, 0.58f, 0.64f, 0.65f);
        ed::Link(link_id(link.id), output_pin_id(link.from_node),
                 input_pin_id(link.to_node), color,
                 link.inferred ? 1.0f : 2.5f);
        if (source && target && !link.label.empty()) {
            link_labels.push_back({source, target, link.label, color});
        }
    }

    ed::End();
    ed::NodeId selected_ids[1];
    const int selected_count = ed::GetSelectedNodes(selected_ids, 1);
    if (selected_count == 1) {
        const int id = static_cast<int>(selected_ids[0].Get());
        if (find_graph_node(*graph, id)) g_selected_graph_node = id;
    } else if (selected_count == 0 &&
               ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
               ImGui::IsItemHovered()) {
        g_selected_graph_node = 0;
    }



    if (!link_labels.empty()) {
        const ImVec2 editor_min = ImGui::GetItemRectMin();
        const ImVec2 editor_max = ImGui::GetItemRectMax();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(editor_min, editor_max, true);
        for (const LinkLabel& label : link_labels) {
            const Quest::GraphNode* source = label.source;
            const Quest::GraphNode* target = label.target;
            const ImVec2 from = ed::GetNodePosition(node_id(source->id));
            const ImVec2 to = ed::GetNodePosition(node_id(target->id));
            const ImVec2 from_size = ed::GetNodeSize(node_id(source->id));
            const ImVec2 to_size = ed::GetNodeSize(node_id(target->id));
            const ImVec2 canvas(
                (from.x + from_size.x + to.x) * 0.5f,
                (from.y + from_size.y * 0.5f +
                 to.y + to_size.y * 0.5f) * 0.5f);
            const ImVec2 screen = ed::CanvasToScreen(canvas);
            const ImVec2 size = ImGui::CalcTextSize(label.text.c_str());
            const ImU32 fill = ImGui::ColorConvertFloat4ToU32(
                ImVec4(0.08f, 0.09f, 0.11f, 0.94f));
            draw->AddRectFilled(ImVec2(screen.x - 5.0f, screen.y - 3.0f),
                                ImVec2(screen.x + size.x + 5.0f,
                                       screen.y + size.y + 3.0f),
                                fill, 4.0f);
            draw->AddText(screen, ImGui::ColorConvertFloat4ToU32(label.color),
                          label.text.c_str());
        }
        draw->PopClipRect();
    }
    if (g_select_completion_requested) {
        std::vector<const Quest::GraphNode*> completions;
        for (const Quest::GraphNode& node : graph->nodes) {
            if (read_only_completion_node(node)) completions.push_back(&node);
        }
        if (g_completion_selection_index < completions.size()) {
            const Quest::GraphNode& completion =
                *completions[g_completion_selection_index];
            ed::ClearSelection();
            ed::SelectNode(node_id(completion.id), false);
            ed::NavigateToSelection(true, 0.0f);
            g_selected_graph_node = completion.id;
            g_completion_focus_frames = 30;
        }
        g_select_completion_requested = false;
        g_focus_quest_requested = false;
        g_fit_requested = false;
    } else if (g_focus_quest_requested) {
        std::vector<const Quest::GraphNode*> opening;
        opening.reserve(graph->nodes.size());
        for (const Quest::GraphNode& node : graph->nodes) {
            opening.push_back(&node);
        }
        if (!g_initial_focus_explicit_range) {
            std::sort(opening.begin(), opening.end(),
                      [](const Quest::GraphNode* a,
                         const Quest::GraphNode* b) {
                          if (a->x != b->x) return a->x < b->x;
                          return a->y < b->y;
                      });
        }
        ed::ClearSelection();
        const std::size_t start = std::min(
            g_initial_focus_explicit_range ? g_initial_focus_node_start : 0,
            opening.size());
        const std::size_t visible =
            std::min(g_initial_focus_node_count, opening.size() - start);
        for (std::size_t i = 0; i < visible; ++i) {
            ed::SelectNode(node_id(opening[start + i]->id), i != 0);
        }
        ed::NavigateToSelection(true, 0.0f);
        g_focus_quest_requested = false;
    } else if (g_fit_requested) {
        ed::NavigateToContent(0.0f);
        g_fit_requested = false;
    }
    if (g_completion_focus_frames > 0 && g_selected_graph_node != 0) {
        ed::ClearSelection();
        ed::SelectNode(node_id(g_selected_graph_node), false);
        ed::NavigateToSelection(true, 0.0f);
        --g_completion_focus_frames;
    }
    ed::SetCurrentEditor(nullptr);
    if (show_inspector) ImGui::EndChild();
    g_apply_layout = false;
}

void RequestSelectCompletionNode(std::size_t index) {
    g_completion_selection_index = index;
    g_select_completion_requested = true;
}

void Shutdown() {
    BlueprintUI::Shutdown();
    if (g_editor) {
        ed::DestroyEditor(g_editor);
        g_editor = nullptr;
    }
    std::lock_guard<std::mutex> lock(g_graph_mutex);
    g_graph.reset();
    ++g_graph_generation;
    g_authored_quests.clear();
    g_active_authored_quest = -1;
    g_selected_graph_node = 0;
}

}
