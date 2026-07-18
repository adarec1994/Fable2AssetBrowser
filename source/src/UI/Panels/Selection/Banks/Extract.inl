void extract_single_bnk_contents(const std::string& bnk_path) {
    if (bnk_path.empty()) {
        OutputLog::warn("Extract BNK: no archive selected.");
        return;
    }

    std::vector<BNKItemUI> items;
    try {
        BNKReader reader(bnk_path);
        const auto& files = reader.list_files();
        items.reserve(files.size());
        for (size_t i = 0; i < files.size(); ++i) {
            BNKItemUI it;
            it.index = (int)i;
            it.name  = files[i].name;
            it.size  = files[i].uncompressed_size;
            items.push_back(std::move(it));
        }
    } catch (const std::exception& e) {
        OutputLog::error(std::string("Extract BNK: failed to open ") +
                         bnk_path + " - " + e.what());
        return;
    } catch (...) {
        OutputLog::error(std::string("Extract BNK: failed to open ") +
                         bnk_path);
        return;
    }
    if (items.empty()) {
        OutputLog::warn(std::string("Extract BNK: ") + bnk_path +
                        " contains no entries.");
        return;
    }

    const std::string export_root =
        S.export_dir.empty() ? std::filesystem::absolute("extracted").string()
                             : S.export_dir;
    const int total = (int)items.size();
    OutputLog::info(std::string("Extracting ") + std::to_string(total) +
                    " file(s) from " +
                    std::filesystem::path(bnk_path).filename().string() +
                    " -> " + export_root);

    progress_open(total,
                  std::string("Extracting ") +
                  std::filesystem::path(bnk_path).filename().string() +
                  " -> " + export_root);
    progress_update(0, total, "Starting...");

    std::thread([items = std::move(items),
                 bnk_path,
                 export_root, total]() {
        struct PG {
            ~PG() { progress_done(); }
        } pg;

        std::atomic<int> done{0};
        std::vector<std::string> failed;
        std::mutex fail_m;

        try {
            for (const auto& it : items) {
                if (S.cancel_requested.load() || S.exiting.load()) break;
                bool ok = false;
                try {

                    extract_file_one(bnk_path, it, export_root,
                                     true);
                    ok = true;
                } catch (const std::exception& e) {
                    OutputLog::error(std::string("Extract failed (") +
                                     it.name + "): " + e.what());
                } catch (...) {
                    OutputLog::error(std::string("Extract failed (") +
                                     it.name + ")");
                }
                if (!ok) {
                    std::lock_guard<std::mutex> lk(fail_m);
                    failed.push_back(it.name);
                }
                int cur = ++done;
                progress_update(cur, total,
                                std::filesystem::path(it.name)
                                    .filename().string());
            }
        } catch (const std::exception& e) {
            OutputLog::error(std::string("Extract worker aborted: ") +
                             e.what());
            return;
        } catch (...) {
            OutputLog::error("Extract worker aborted (unknown exception).");
            return;
        }

        if (S.cancel_requested.load()) {
            OutputLog::warn(std::string("BNK extract cancelled (") +
                            std::to_string(done.load()) + "/" +
                            std::to_string(total) + ").");
            S.cancel_requested = false;
            return;
        }
        const int n_failed = (int)failed.size();
        if (n_failed > 0) {
            OutputLog::warn(std::string("BNK extract finished: ") +
                            std::to_string(done.load() - n_failed) + "/" +
                            std::to_string(total) + " written, " +
                            std::to_string(n_failed) + " failed.");
        } else {
            OutputLog::success(std::string("BNK extract complete: ") +
                               std::to_string(total) + " file(s) -> " +
                               export_root);
        }
    }).detach();
}
