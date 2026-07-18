void dump_single_lua_file(const std::string& path, const std::string& root_dir) {
    std::string bank_name = determine_bank_name(path);
    std::string out_path = build_output_path(path, bank_name);

    if (out_path.empty() || path == out_path) {
        std::filesystem::path rel = std::filesystem::relative(path, root_dir);
        out_path = (std::filesystem::path(root_dir) / "extracted" / "lua" / rel).string();
    }

    std::filesystem::path out_p(out_path);
    std::error_code ec;
    std::filesystem::create_directories(out_p.parent_path(), ec);

    std::string content = read_lua_file_content(path);

    std::ofstream out(out_path);
    if (out.is_open()) {
        out << content;
        out.close();
        show_completion_box("Dumped to:\n" + out_path);
    } else {
        show_error_box("Failed to write:\n" + out_path);
    }
}
