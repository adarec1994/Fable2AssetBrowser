struct EntityPreviewCompletion {
    std::uint64_t request = 0;
    int entity_index = -1;
    MDLInfo model_info;
    std::vector<MDLMeshGeom> meshes;
    std::string primary_model_path;
    std::uint32_t primary_model_hash = 0;
};

std::atomic<std::uint64_t> g_entity_preview_request{0};
std::mutex g_entity_preview_mutex;
std::vector<EntityPreviewCompletion> g_entity_preview_completions;

NpcAuthoring::Definition g_new_npc;
int g_new_npc_template_index = -1;
char g_new_npc_template_filter[128]{};
std::string g_new_npc_error;
StaticPropAuthoring::Definition g_new_static_prop;
int g_new_static_prop_model_index = -1;
char g_new_static_prop_model_filter[128]{};
std::string g_new_static_prop_error;
enum class NewEntityKind : int {
    Npc = 0,
    StaticProp = 1,
};
NewEntityKind g_new_entity_kind = NewEntityKind::Npc;
bool g_open_create_npc_requested = false;

void select_new_npc_template(int index) {
    if (index < 0 ||
        static_cast<std::size_t>(index) >= g_global_entity_catalog.size()) {
        return;
    }
    if (g_global_entity_catalog[static_cast<std::size_t>(index)].kind !=
        Gdb::EntityCatalogKind::Creature) {
        return;
    }
    const std::string internal_name = g_new_npc.internal_name;
    const std::string display_name = g_new_npc.display_name;
    g_new_npc = NpcAuthoring::Definition{};
    g_new_npc.internal_name = internal_name;
    g_new_npc.display_name = display_name;
    g_new_npc_template_index = index;

    const Gdb::CreatureCatalogEntry& entity =
        g_global_entity_catalog[static_cast<std::size_t>(index)];
    g_new_npc.template_name = entity.display_name.empty()
        ? entity.name : entity.display_name;
    g_new_npc.template_entity = entity.entity_hash;
    g_new_npc.model_hashes = entity.model_hashes;

    const auto gameplay = g_global_entity_gameplay.find(entity.entity_hash);
    if (gameplay == g_global_entity_gameplay.end()) {
        g_new_npc_error =
            "That entity has no indexed NPC gameplay components.";
        return;
    }
    const Gdb::EntityGameplayDetails& details = gameplay->second;
    g_new_npc.creature_component = details.creature_component_record;
    g_new_npc.health_component = details.health_component_record;
    g_new_npc.combat_component = details.combat_component_record;
    g_new_npc.faction_component = details.faction_component_record;
    g_new_npc.faction_record = details.faction_record;
    g_new_npc.faction_name = details.faction_name;
    g_new_npc.combat_profile_record = details.combat_profile_record;
    g_new_npc.combat_profile_name = details.combat_profile_name;
    for (const auto& option : g_global_entity_gameplay_options.factions) {
        if (option.record_hash == g_new_npc.faction_record) {
            g_new_npc.faction_name = option.label;
            break;
        }
    }
    for (const auto& option :
         g_global_entity_gameplay_options.combat_profiles) {
        if (option.record_hash == g_new_npc.combat_profile_record) {
            g_new_npc.combat_profile_name = option.label;
            break;
        }
    }
    for (const Gdb::EntityGameplayField& source : details.core_fields) {
        NpcAuthoring::FieldValue value;
        value.label = source.label;
        value.display_value = source.value;
        value.field_hash = source.field_hash;
        value.raw_value = source.raw_value;
        value.value_type = source.value_type;
        g_new_npc.core_fields.push_back(std::move(value));
    }
    for (const Gdb::EntityGameplayField& source : details.combat_fields) {
        NpcAuthoring::FieldValue value;
        value.label = source.label;
        value.display_value = source.value;
        value.field_hash = source.field_hash;
        value.raw_value = source.raw_value;
        value.value_type = source.value_type;
        g_new_npc.combat_fields.push_back(std::move(value));
    }
    g_new_npc_error.clear();
}
