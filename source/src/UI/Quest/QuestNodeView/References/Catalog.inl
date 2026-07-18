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
