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
