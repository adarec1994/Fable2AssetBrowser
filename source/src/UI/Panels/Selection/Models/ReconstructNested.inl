bool reconstruct_nested_mdl(const std::string& nested_bnk_path, int file_index, std::vector<unsigned char>& out, const std::string& mdl_full_path) {
    try {
        BNKReader nested_reader(nested_bnk_path);
        const auto& files = nested_reader.list_files();
        if (file_index < 0 || file_index >= (int)files.size()) return false;

        std::string mdl_name = files[file_index].name;

        auto tmpdir = std::filesystem::temp_directory_path() / "f2_nested_mdl_reconstruct";
        std::error_code ec;
        std::filesystem::create_directories(tmpdir, ec);

        auto tmp_body = tmpdir / "body.bin";
        extract_one(nested_bnk_path, file_index, tmp_body.string());
        auto body_data = read_all_bytes(tmp_body);
        std::filesystem::remove(tmp_body, ec);

        if (body_data.empty()) return false;

        auto p_headers = find_bnk_by_filename("globals_model_headers.bnk");
        if (!p_headers) {
            out = body_data;
            return true;
        }

        BNKReader r_headers(*p_headers);
        const auto& header_files = r_headers.list_files();

        auto norm = [](std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), ::tolower);
            std::replace(s.begin(), s.end(), '\\', '/');
            return s;
        };
        std::vector<std::string> full_keys;
        if (!mdl_full_path.empty()) full_keys.push_back(norm(mdl_full_path));
        if (mdl_name.find('/') != std::string::npos ||
            mdl_name.find('\\') != std::string::npos)
            full_keys.push_back(norm(mdl_name));
        const std::string base_key =
            norm(std::filesystem::path(mdl_name).filename().string());

        int header_idx = -1;
        for (const auto& want : full_keys) {
            for (size_t i = 0; i < header_files.size(); ++i) {
                if (norm(header_files[i].name) == want) { header_idx = (int)i; break; }
            }
            if (header_idx != -1) break;
        }
        if (header_idx == -1) {
            for (size_t i = 0; i < header_files.size(); ++i) {
                std::string hbase =
                    norm(std::filesystem::path(header_files[i].name).filename().string());
                if (hbase == base_key) { header_idx = (int)i; break; }
            }
        }

        if (header_idx == -1) {
            OutputLog::warn("nested MDL: no header match for body '" + mdl_name +
                            "' (full='" + mdl_full_path + "') -> body-only (will not parse)");
            out = body_data;
            return true;
        }
        OutputLog::info("nested MDL: body '" + mdl_name + "' (full='" + mdl_full_path +
                        "') -> header[" + std::to_string(header_idx) + "] '" +
                        header_files[header_idx].name + "'");

        auto tmp_header = tmpdir / "header.bin";
        extract_one(*p_headers, header_idx, tmp_header.string());
        auto header_data = read_all_bytes(tmp_header);
        std::filesystem::remove(tmp_header, ec);

        if (header_data.empty()) {
            out = body_data;
            return true;
        }

        out.clear();
        out.reserve(header_data.size() + body_data.size());
        out.insert(out.end(), header_data.begin(), header_data.end());
        out.insert(out.end(), body_data.begin(), body_data.end());

        return true;

    } catch (...) {
        return false;
    }
}
