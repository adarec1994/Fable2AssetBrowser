    size_t text_written = 0;
    if (rebuilt && contents_ok && !babel_edits.empty()) {
        progress_update(92, 100, "Writing text banks...");
        std::string root = S.root_dir;
        {
            std::error_code ec;
            std::filesystem::path rp(root);
            if (!root.empty() &&
                std::filesystem::is_regular_file(rp, ec)) {
                root = rp.parent_path().string();
            }
        }
        std::string terr;
        if (TextBank::ApplyEdits(root, babel_edits, terr)) {
            text_written = babel_edits.size();
        } else {
            contents_ok = false;
            OutputLog::error("level edit: text bank write failed: " +
                             terr);
        }
    }
