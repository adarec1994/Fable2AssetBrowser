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
