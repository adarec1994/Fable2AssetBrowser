void open_tree_bnk_drill_from_entry(const std::string& parent_bnk_path,
                                    int file_index,
                                    const std::string& entry_name) {
    if (parent_bnk_path.empty() || file_index < 0) return;

    const std::filesystem::path tmpdir =
        std::filesystem::temp_directory_path() / "f2_tree_bnk_drill";
    std::error_code ec;
    std::filesystem::create_directories(tmpdir, ec);

    const std::string temp_name =
        std::to_string(std::hash<std::string>{}(
            parent_bnk_path + "::" + entry_name + "::" +
            std::to_string(file_index))) + ".bnk";
    const std::filesystem::path tmp_bnk = tmpdir / temp_name;

    try {
        extract_one(parent_bnk_path, file_index, tmp_bnk.string());
        drill_open_bnk(g_tree_drill, tmp_bnk.string(), true);
        const std::string title =
            std::filesystem::path(entry_name).filename().string();
        if (!title.empty()) {
            g_tree_drill.title = title;
        }
    } catch (const std::exception& e) {
        OutputLog::error(std::string("File tree BNK open failed: ") +
                         e.what());
    } catch (...) {
        OutputLog::error("File tree BNK open failed.");
    }
}
