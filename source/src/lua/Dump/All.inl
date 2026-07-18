void dump_all_lua_files(const std::string& root_dir) {
    auto lua_paths = scan_luas_recursive(root_dir);

    if (lua_paths.empty()) {
        show_completion_box("No Lua files found.");
        return;
    }

    progress_open((int)lua_paths.size(), "Dumping Lua files...");

    std::thread([lua_paths, root_dir]() {
        int success_count = 0;
        int fail_count = 0;

        for (size_t i = 0; i < lua_paths.size(); ++i) {
            if (S.cancel_requested) break;

            std::filesystem::path p(lua_paths[i]);
            progress_update((int)i + 1, (int)lua_paths.size(), p.filename().string());

            std::string bank_name = determine_bank_name(lua_paths[i]);
            std::string out_path = build_output_path(lua_paths[i], bank_name);

            if (out_path.empty() || lua_paths[i] == out_path) {
                std::filesystem::path rel = std::filesystem::relative(lua_paths[i], root_dir);
                out_path = (std::filesystem::path(root_dir) / "extracted" / "lua" / rel).string();
            }

            std::filesystem::path out_p(out_path);
            std::error_code ec;
            std::filesystem::create_directories(out_p.parent_path(), ec);

            if (lua_paths[i] == out_path) {
                fail_count++;
                continue;
            }

            std::string content = read_lua_file_content(lua_paths[i]);

            std::ofstream out(out_path);
            if (out.is_open()) {
                out << content;
                out.close();
                success_count++;
            } else {
                fail_count++;
            }
        }

        progress_done();

        std::string msg = "Lua dump complete.\n";
        msg += "Processed: " + std::to_string(success_count);
        if (fail_count > 0) {
            msg += "\nFailed: " + std::to_string(fail_count);
        }

        show_completion_box(msg);
    }).detach();
}
