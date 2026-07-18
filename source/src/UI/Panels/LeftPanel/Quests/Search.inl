bool select_quest_script_by_query(const std::string& query) {
    std::string needle = query;
    std::transform(needle.begin(), needle.end(), needle.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });

    for (size_t i = 0; i < S.all_quest_files.size(); ++i) {
        const FlatAssetEntry& entry = S.all_quest_files[i];
        std::string haystack = entry.name + " " + entry.full_path;
        std::transform(haystack.begin(), haystack.end(), haystack.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });
        if (haystack.find(needle) == std::string::npos) continue;

        S.quest_filter = query;
        select_quest_script(i);
        return true;
    }
    return false;
}
