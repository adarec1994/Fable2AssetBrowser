#include "BlueprintEditor.h"

#include "BlueprintCanvas.h"
#include "BlueprintInspector.h"
#include "Quest/Blueprint/BlueprintCompiler.h"
#include "Quest/Blueprint/BlueprintNodeRegistry.h"
#include "Quest/Blueprint/BlueprintSerialize.h"
#include "Quest/QuestAuthoring.h"
#include "UI/OutputLog.h"
#include "Utilities/State.h"

#include "IconsFontAwesome6.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <imgui_node_editor.h>

#include <filesystem>
#include <map>
#include <sstream>

namespace BlueprintUI {

using namespace Quest::Bp;
using BlueprintUIDetail::CanvasState;

namespace {

struct Doc {
    BlueprintQuest quest;
    CanvasState    canvas;
    uint64_t       saved_revision = 0;
};

std::map<std::string, Doc>& docs() {
    static std::map<std::string, Doc> d;
    return d;
}

std::string& active_id() {
    static std::string id;
    return id;
}

Doc* active_doc() {
    if (active_id().empty()) return nullptr;
    auto it = docs().find(active_id());
    return it == docs().end() ? nullptr : &it->second;
}

std::filesystem::path quests_dir() {
    return std::filesystem::current_path() / "authored_quests";
}

std::filesystem::path quest_file(const std::string& quest_id) {
    return quests_dir() / (quest_id + ".bpquest.json");
}

void save_doc(Doc& doc) {
    if (doc.saved_revision == doc.quest.revision) return;
    std::string error;
    if (SaveToFile(doc.quest, quest_file(doc.quest.quest_id).string(),
                   error)) {
        doc.saved_revision = doc.quest.revision;
    } else {
        OutputLog::warn("blueprint save failed: " + error);
    }
}



void ensure_loaded() {
    static bool done = false;
    if (done) return;
    done = true;
    std::error_code ec;
    if (!std::filesystem::is_directory(quests_dir(), ec)) return;
    size_t loaded = 0;
    for (std::filesystem::directory_iterator it(quests_dir(), ec), end;
         !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.size() < 14 ||
            name.compare(name.size() - 13, 13, ".bpquest.json") != 0) {
            continue;
        }
        BlueprintQuest quest;
        std::string error;
        if (!LoadFromFile(it->path().string(), quest, error)) {
            OutputLog::warn("blueprint load failed (" + name + "): " +
                            error);
            continue;
        }
        if (docs().count(quest.quest_id)) continue;
        Doc doc;
        doc.quest = std::move(quest);
        doc.saved_revision = doc.quest.revision;
        docs().emplace(doc.quest.quest_id, std::move(doc));
        ++loaded;
    }
    if (loaded) {
        OutputLog::info("blueprint quests: restored " +
                        std::to_string(loaded) + " from authored_quests/");
    }
}

}

bool CreateQuest(const std::string& quest_id, std::string& error) {
    ensure_loaded();
    if (!Quest::IsValidQuestId(quest_id)) {
        error = "Quest IDs use letters, digits and underscores "
                "(e.g. QO900_MyQuest).";
        return false;
    }
    if (docs().count(quest_id)) {
        error = "A blueprint quest with this ID already exists.";
        return false;
    }
    Doc doc;
    doc.quest.quest_id = quest_id;
    doc.quest.quest_title = quest_id;
    Registry::Instantiate(doc.quest, "event.quest_start", 0.0f, 0.0f);
    docs().emplace(quest_id, std::move(doc));
    active_id() = quest_id;
    return true;
}

bool OpenQuest(const std::string& quest_id) {
    ensure_loaded();
    if (!docs().count(quest_id)) return false;
    if (Doc* previous = active_doc()) save_doc(*previous);
    active_id() = quest_id;
    return true;
}

bool HasQuest(const std::string& quest_id) {
    ensure_loaded();
    return docs().count(quest_id) != 0;
}

bool DeleteQuest(const std::string& quest_id, std::string& error) {
    ensure_loaded();
    auto it = docs().find(quest_id);
    if (it == docs().end()) {
        error = "No blueprint quest named " + quest_id + ".";
        return false;
    }
    BlueprintUIDetail::DestroyCanvas(it->second.canvas);
    docs().erase(it);
    if (active_id() == quest_id) active_id().clear();
    std::error_code ec;
    std::filesystem::remove(quest_file(quest_id), ec);
    if (ec) {
        error = "Removed from the editor, but could not delete " +
                quest_file(quest_id).string() + ": " + ec.message();
        return false;
    }
    return true;
}

std::vector<std::string> QuestIds() {
    ensure_loaded();
    std::vector<std::string> out;
    out.reserve(docs().size());
    for (const auto& [id, doc] : docs()) out.push_back(id);
    return out;
}

bool IsActive() { return active_doc() != nullptr; }

std::string ActiveQuestId() { return active_id(); }

BlueprintQuest* ActiveQuest() {
    Doc* doc = active_doc();
    return doc ? &doc->quest : nullptr;
}

void CloseActive() {
    if (Doc* doc = active_doc()) save_doc(*doc);
    active_id().clear();
}

namespace {



const CompileResult& compiled(Doc& doc) {
    static std::map<std::string, std::pair<uint64_t, CompileResult>> cache;
    static std::map<std::string, std::string> logged;
    auto& slot = cache[doc.quest.quest_id];
    if (slot.first != doc.quest.revision + 1) {
        slot.first = doc.quest.revision + 1;
        slot.second = Compile(doc.quest);

        
        
        std::string sig;
        for (const Diagnostic& d : slot.second.diagnostics) {
            sig += d.severity == Severity::Error ? 'E' : 'W';
            sig += d.message;
            sig += '\n';
        }
        std::string& last = logged[doc.quest.quest_id];
        if (sig != last) {
            last = sig;
            for (const Diagnostic& d : slot.second.diagnostics) {
                const std::string line =
                    doc.quest.quest_id + ": " + d.message;
                if (d.severity == Severity::Error) {
                    OutputLog::error(line);
                } else {
                    OutputLog::warn(line);
                }
            }
        }
    }
    return slot.second;
}



Quest::AuthoredQuest eligibility_shim(const BlueprintQuest& quest) {
    Quest::AuthoredQuest shim;
    shim.quest_id = quest.quest_id;
    shim.quest_title = quest.quest_title;
    shim.prerequisites = CollectPrerequisites(quest);
    return shim;
}


std::string s_dropped_var;
float       s_drop_x = 0.0f;
float       s_drop_y = 0.0f;

void spawn_variable_node(Doc& doc, const char* type,
                         const std::string& var_name, float x, float y) {
    const int id = Registry::Instantiate(doc.quest, type, x, y);
    if (!id) return;
    Quest::Bp::Node* node = doc.quest.NodeById(id);
    node->prop = var_name;
    Registry::SyncVariableNode(doc.quest, *node);
    doc.canvas.layout_pending = true;   
    doc.canvas.place_topright_node = id;
    doc.canvas.place_pos_x = x;
    doc.canvas.place_pos_y = y;
    doc.quest.Touch();
}

}

std::string ActiveQuestLua() {
    Doc* doc = active_doc();
    if (!doc) return {};
    return compiled(*doc).quest_lua;
}

std::string ActiveEligibilityLua() {
    Doc* doc = active_doc();
    if (!doc) return {};
    return Quest::GenerateEligibilityLua(eligibility_shim(doc->quest));
}

std::vector<std::pair<std::string, std::string>> ActiveTextEntries() {
    Doc* doc = active_doc();
    if (!doc) return {};
    return compiled(*doc).text_entries;
}

std::string ActiveLua() {
    Doc* doc = active_doc();
    if (!doc) return {};
    const CompileResult& result = compiled(*doc);
    std::ostringstream os;
    if (!result.diagnostics.empty()) {
        int errors = 0;
        int warnings = 0;
        for (const Diagnostic& d : result.diagnostics) {
            (d.severity == Severity::Error ? errors : warnings) += 1;
        }
        os << "-- " << errors << " error(s), " << warnings
           << " warning(s):\n";
        for (const Diagnostic& d : result.diagnostics) {
            os << "--   ["
               << (d.severity == Severity::Error ? "error" : "warning")
               << "] node " << d.node_id << ": " << d.message << "\n";
        }
        os << "\n";
    }
    if (!result.HasErrors()) {
        os << "-- scripts\\quests\\" << doc->quest.quest_id << ".lua\n\n"
           << result.quest_lua << "\n"
           << "-- gameflow eligibility (patched into gameflow.lua):\n-- "
           << Quest::GenerateEligibilityLua(eligibility_shim(doc->quest));
    }
    return os.str();
}

bool ValidateActive(std::string& error) {
    Doc* doc = active_doc();
    if (!doc) {
        error = "No blueprint quest is active.";
        return false;
    }
    const CompileResult& result = compiled(*doc);
    if (result.HasErrors()) {
        for (const Diagnostic& d : result.diagnostics) {
            if (d.severity == Severity::Error) {
                error = d.message;
                return false;
            }
        }
    }
    return true;
}

std::vector<Quest::Bp::Diagnostic> ActiveDiagnostics() {
    Doc* doc = active_doc();
    if (!doc) return {};
    return compiled(*doc).diagnostics;
}

void Draw() {
    Doc* doc = active_doc();
    if (!doc) return;

    BlueprintQuest& quest = doc->quest;
    CanvasState& canvas = doc->canvas;

    const std::vector<Diagnostic>& diagnostics = compiled(*doc).diagnostics;

    constexpr float inspector_width = 340.0f;

    {
        ImGui::BeginChild("##bp_inspector",
                          ImVec2(inspector_width, 0.0f), true);
        if (canvas.selected_node != 0) {
            BlueprintUIDetail::DrawNodeInspector(quest,
                                                 canvas.selected_node);
            ImGui::Separator();
        } else {
            ImGui::TextDisabled("Select a node to edit its values.");
            ImGui::Separator();
        }
        BlueprintUIDetail::DrawQuestInspector(quest);
        ImGui::EndChild();
        ImGui::SameLine();
    }

    ImGui::BeginChild("##bp_canvas_child", ImVec2(0.0f, 0.0f), false);
    const ImGuiID canvas_dnd_id = ImGui::GetID("##bp_canvas_dnd");
    BlueprintUIDetail::DrawCanvas(quest, canvas);

    
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (ImGui::BeginDragDropTargetCustom(window->Rect(),
                                             canvas_dnd_id)) {
            if (const ImGuiPayload* payload =
                    ImGui::AcceptDragDropPayload("BP_VARIABLE")) {
                s_dropped_var.assign((const char*)payload->Data);
                s_drop_x = canvas.spawn_x;
                s_drop_y = canvas.spawn_y;
                if (ImGui::GetIO().KeyCtrl) {
                    spawn_variable_node(*doc, "var.get", s_dropped_var,
                                        s_drop_x, s_drop_y);
                    s_dropped_var.clear();
                } else if (ImGui::GetIO().KeyAlt) {
                    spawn_variable_node(*doc, "var.set", s_dropped_var,
                                        s_drop_x, s_drop_y);
                    s_dropped_var.clear();
                } else {
                    ImGui::OpenPopup("##bp_var_drop");
                }
            }
            ImGui::EndDragDropTarget();
        }
        if (ImGui::BeginPopup("##bp_var_drop")) {
            ImGui::TextDisabled("%s", s_dropped_var.c_str());
            ImGui::Separator();
            if (ImGui::MenuItem(("Get " + s_dropped_var).c_str())) {
                spawn_variable_node(*doc, "var.get", s_dropped_var,
                                    s_drop_x, s_drop_y);
            }
            if (ImGui::MenuItem(("Set " + s_dropped_var).c_str())) {
                spawn_variable_node(*doc, "var.set", s_dropped_var,
                                    s_drop_x, s_drop_y);
            }
            ImGui::EndPopup();
        }
    }
    ImGui::EndChild();

    
    
    static std::string last_quest;
    static uint64_t last_revision = ~0ull;
    if (last_quest != quest.quest_id || last_revision != quest.revision) {
        last_quest = quest.quest_id;
        last_revision = quest.revision;
        canvas.node_diag.clear();
        for (const Diagnostic& d : diagnostics) {
            int& slot = canvas.node_diag[d.node_id];
            slot = std::max(slot, d.severity == Severity::Error ? 2 : 1);
        }
        if (S.lua_preview_is_quest) {
            S.lua_preview_content = ActiveLua();
        }
        save_doc(*doc);
    }
}

namespace {
int s_pending_pick_pin = 0;
}

void ArmPinPick(int pin_id) { s_pending_pick_pin = pin_id; }

int PendingPickPin() { return s_pending_pick_pin; }

bool BindPendingPin(const std::string& level_path,
                    const std::string& level_id,
                    const std::string& entity_name, uint32_t entity_hash,
                    float x, float y, float z,
                    const std::vector<uint32_t>& model_hashes,
                    bool authored_instance, std::string& error) {
    Doc* doc = active_doc();
    if (!doc || s_pending_pick_pin == 0) {
        error = "No blueprint pin is waiting for a level pick.";
        return false;
    }
    Quest::Bp::Pin* pin = doc->quest.PinById(s_pending_pick_pin);
    if (!pin) {
        error = "The armed pin no longer exists.";
        s_pending_pick_pin = 0;
        return false;
    }
    pin->value.world.level_path = level_path;
    pin->value.world.level_id = level_id;
    pin->value.world.entity_name = entity_name;
    pin->value.world.entity_hash = entity_hash;
    pin->value.world.x = x;
    pin->value.world.y = y;
    pin->value.world.z = z;
    pin->value.world.model_hashes = model_hashes;
    pin->value.world.authored_instance = authored_instance;
    doc->quest.Touch();
    s_pending_pick_pin = 0;
    return true;
}

void Shutdown() {
    for (auto& [id, doc] : docs()) {
        save_doc(doc);
        BlueprintUIDetail::DestroyCanvas(doc.canvas);
    }
    docs().clear();
    active_id().clear();
}

}
