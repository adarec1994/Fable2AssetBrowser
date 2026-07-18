struct ContainerSpawnChoice {
    std::string label;
    std::string model_path;
    bool is_dive = false;
    LevelEdit::ContainerTemplateInfo info;
};

static uint32_t level_model_path_hash(const std::string& path)
{
    uint32_t hash = 0x811C9DC5u;
    for (unsigned char c : path) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<unsigned char>(c - 'A' + 'a');
        }
        if (c == '/') c = '\\';
        hash *= 0x01000193u;
        hash ^= uint32_t(c);
    }
    return hash;
}

static std::string container_spawn_label(std::string value)
{
    const size_t slash = value.find_last_of("/\\");
    if (slash != std::string::npos) value.erase(0, slash + 1);
    const size_t dot = value.rfind('.');
    if (dot != std::string::npos) value.resize(dot);
    for (char& c : value) {
        if (c == '_' || c == '-') c = ' ';
    }
    if (value.empty()) value = "Unnamed container";
    return value;
}

static std::vector<ContainerSpawnChoice> build_container_spawn_choices()
{
    static std::string cached_level;
    static size_t cached_contents = size_t(-1);
    static size_t cached_models = size_t(-1);
    static std::vector<ContainerSpawnChoice> cached;
    if (cached_level == g_pending_terrain_label &&
        cached_contents == g_level_entity_contents.size() &&
        cached_models == S.all_mdl_files.size()) {
        return cached;
    }
    std::unordered_map<uint32_t, std::string> models_by_hash;
    models_by_hash.reserve(S.all_mdl_files.size() * 2);
    for (const auto& model : S.all_mdl_files) {
        models_by_hash.emplace(level_model_path_hash(model.full_path),
                               model.full_path);
    }
    std::unordered_map<uint32_t, uint32_t> placement_models;
    for (const auto& placement : g_level_gdb_placements) {
        if (placement.hash && placement.model_path_hash) {
            placement_models.emplace(placement.hash,
                                     placement.model_path_hash);
        }
    }

    std::vector<ContainerSpawnChoice> out;
    std::unordered_set<std::string> seen;
    for (const auto& [entity_hash, contents] : g_level_entity_contents) {
        if (!contents.is_dig_spot && !contents.is_dive_spot &&
            g_level_entity_gameplay.count(entity_hash)) {
            continue;
        }
        if (!contents.is_dig_spot && !contents.is_dive_spot &&
            !contents.has_inventory_component &&
            !contents.has_chest_component) {
            continue;
        }

        uint32_t model_hash = contents.model_path_hash;
        if (!model_hash) {
            const auto placed = placement_models.find(entity_hash);
            if (placed != placement_models.end()) model_hash = placed->second;
        }
        ContainerSpawnChoice choice;
        choice.is_dive = contents.is_dive_spot;
        choice.info.entity_name = contents.entity_name;
        choice.info.is_dig_spot = contents.is_dig_spot;
        choice.info.silver_keys_needed =
            std::max(0, contents.silver_keys_needed);
        choice.info.entity_template = contents.entity_template;
        choice.info.transform_component_field =
            contents.transform_component_field;
        choice.info.transform_component_template =
            contents.transform_component_template;
        choice.info.physics_file_hash = contents.physics_file_hash;
        choice.info.potential_items_record =
            contents.potential_items_record;
        for (const auto& item : contents.initial_items) {
            choice.info.initial_items.push_back(item.record_hash);
        }
        if (model_hash) {
            const auto model = models_by_hash.find(model_hash);
            if (model != models_by_hash.end()) {
                choice.model_path = model->second;
            }
            const auto prop =
                g_level_prop_entity_templates.find(model_hash);
            if (prop != g_level_prop_entity_templates.end()) {
                const auto& donor = prop->second;
                if (!choice.info.entity_template) {
                    choice.info.entity_template = donor.template_hash;
                }
                if (!choice.info.transform_component_field) {
                    choice.info.transform_component_field =
                        donor.comp_field_hash;
                }
                if (!choice.info.transform_component_template) {
                    choice.info.transform_component_template =
                        donor.comp_template_hash;
                }
                if (!choice.info.physics_file_hash) {
                    choice.info.physics_file_hash = donor.physics_file_hash;
                }
            }
        }
        if (!choice.info.entity_template ||
            !choice.info.transform_component_field) {
            continue;
        }
        choice.label = container_spawn_label(
            !choice.model_path.empty() ? choice.model_path
                                       : choice.info.entity_name);
        char key[96];
        std::snprintf(key, sizeof(key), "%d:%08X:%08X",
                      choice.info.is_dig_spot ? 1 : 0,
                      choice.info.entity_template, model_hash);
        if (!seen.insert(key).second) continue;
        out.push_back(std::move(choice));
    }
    std::sort(out.begin(), out.end(),
              [](const auto& a, const auto& b) {
                  if (a.info.is_dig_spot != b.info.is_dig_spot) {
                      return !a.info.is_dig_spot;
                  }
                  return a.label < b.label;
              });
    cached_level = g_pending_terrain_label;
    cached_contents = g_level_entity_contents.size();
    cached_models = S.all_mdl_files.size();
    cached = out;
    return cached;
}
