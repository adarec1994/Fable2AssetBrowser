static std::string quest_level_id_from_path(std::string path)
{
    std::replace(path.begin(), path.end(), '/', '\\');
    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    const std::string root = "worlds\\albion\\";
    const size_t root_pos = lower.find(root);
    if (root_pos != std::string::npos) {
        path.erase(0, root_pos + root.size());
        lower.erase(0, root_pos + root.size());
    }
    const std::string suffix =
        "\\defaultscenario\\defaultscenario.engine_level";
    const size_t suffix_pos = lower.rfind(suffix);
    if (suffix_pos != std::string::npos) path.resize(suffix_pos);
    if (path.empty()) path = g_pending_terrain_label;
    return path;
}

static bool build_quest_level_reference(
    const LevelSpawnMarker& marker,
    size_t marker_index,
    QuestUI::LevelReferenceCandidate& candidate)
{
    candidate = QuestUI::LevelReferenceCandidate{};
    candidate.is_npc = marker.kind == 3;
    candidate.is_container = marker.is_container;
    candidate.level_path = g_pending_terrain_level_entry.full_path;
    candidate.level_id = quest_level_id_from_path(candidate.level_path);
    candidate.entity_name = marker.name;
    candidate.entity_hash = marker.entity_hash;
    candidate.x = marker.x;
    candidate.y = marker.y;
    candidate.z = marker.z;
    float position_delta[3] = {};
    float rotation_delta[3] = {};
    if (LevelEdit::EditFor(0x70000000u | uint32_t(marker_index),
                           position_delta, rotation_delta)) {
        candidate.x += position_delta[0];
        candidate.y += position_delta[1];
        candidate.z += position_delta[2];
    }
    candidate.model_hashes = marker.model_hashes;
    candidate.authored_instance = marker.pending_addition_index >= 0 &&
        LevelEdit::AdditionIsNamedEntity(
            marker.pending_addition_index);
    if (candidate.entity_name.empty()) {
        const auto contents =
            g_level_entity_contents.find(candidate.entity_hash);
        if (contents != g_level_entity_contents.end()) {
            candidate.entity_name = contents->second.entity_name;
        }
    }
    return !candidate.level_id.empty() &&
           !candidate.entity_name.empty() &&
           (candidate.entity_hash != 0 || candidate.authored_instance);
}

static std::string unique_static_prop_instance_name(
    const Gdb::CreatureCatalogEntry& entity)
{
    std::string base = "F2AB_Static_" + entity.name;
    for (char& c : base) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
            c = '_';
        }
    }
    auto used = [&](const std::string& candidate) {
        for (const LevelSpawnMarker& marker : g_level_spawn_markers) {
            if (marker.name == candidate) return true;
        }
        std::vector<LevelEdit::Addition> additions;
        LevelEdit::GetAdditions(additions);
        return std::any_of(
            additions.begin(), additions.end(),
            [&](const LevelEdit::Addition& addition) {
                return !addition.removed &&
                       addition.entity_name == candidate;
            });
    };
    if (!used(base)) return base;
    for (unsigned int suffix = 2; suffix < 100000; ++suffix) {
        const std::string candidate =
            base + '_' + std::to_string(suffix);
        if (!used(candidate)) return candidate;
    }
    return base + "_New";
}
