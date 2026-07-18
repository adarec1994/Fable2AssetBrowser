void dump_bnk_contents() {

    std::vector<std::string> all_bnks;
    all_bnks.reserve(S.bnk_paths.size() + S.nested_bnk_paths.size());
    all_bnks.insert(all_bnks.end(),
                    S.bnk_paths.begin(), S.bnk_paths.end());
    all_bnks.insert(all_bnks.end(),
                    S.nested_bnk_paths.begin(), S.nested_bnk_paths.end());

    if (all_bnks.empty()) {
        OutputLog::warn("Dump BNK contents: no BNKs indexed (open a "
                        "Fable 2 root first).");
        return;
    }

    const std::string export_root =
        S.export_dir.empty() ? std::filesystem::absolute("extracted").string()
                             : S.export_dir;

    OutputLog::info(std::string("Dumping every file in ") +
                    std::to_string(all_bnks.size()) + " BNK(s) -> " +
                    export_root);

    progress_open(0, std::string("Dumping BNK contents -> ") + export_root);
    progress_update(0, 0, "Indexing...");

    std::thread([all_bnks = std::move(all_bnks), export_root]() {
        struct DumpGuard {
            ~DumpGuard() { progress_done(); }
        } pg;

        std::unordered_map<std::string, BnkCacheEntry> bnk_cache;
        int total = 0;
        for (const auto& bp : all_bnks) {
            if (S.cancel_requested.load() || S.exiting.load()) break;
            auto* ce = get_or_open_bnk(bnk_cache, bp);
            if (!ce) {
                OutputLog::error(std::string(
                    "BNK contents dump: cannot open ") + bp);
                continue;
            }
            total += (int)ce->reader->list_files().size();
        }
        if (total <= 0) {
            OutputLog::warn("BNK contents dump: every BNK was empty / "
                            "unreadable.");
            return;
        }

        progress_update(0, total, "Starting...");

        std::atomic<int> done{0};
        std::vector<std::string> failed;
        std::mutex fail_m;

        try {
            for (const auto& bp : all_bnks) {
                if (S.cancel_requested.load() || S.exiting.load()) break;
                auto it = bnk_cache.find(bp);
                if (it == bnk_cache.end() || !it->second.reader) continue;
                auto& ce = it->second;
                const auto& files = ce.reader->list_files();

                std::string stem = std::filesystem::path(bp)
                                       .stem().string();
                std::filesystem::path bnk_root =
                    std::filesystem::path(export_root) / stem;

                for (size_t i = 0; i < files.size(); ++i) {
                    if (S.cancel_requested.load() || S.exiting.load()) break;
                    const auto& fe = files[i];

                    std::string rel = fe.name;
                    while (!rel.empty() &&
                           (rel.front() == '/' || rel.front() == '\\'))
                        rel.erase(rel.begin());
                    auto out = bnk_root / rel;

                    bool ok = false;
                    try {
                        std::error_code ec;
                        if (auto parent = out.parent_path(); !parent.empty()) {
                            std::filesystem::create_directories(parent, ec);
                        }

                        extract_one(bp, (int)i, out.string());
                        ok = std::filesystem::exists(out, ec) && !ec;
                    } catch (const std::exception& ex) {
                        OutputLog::error(std::string(
                            "BNK contents exception on ") + bp + " :: " +
                            fe.name + ": " + ex.what());
                    } catch (...) {
                        OutputLog::error(std::string(
                            "BNK contents exception on ") + bp + " :: " +
                            fe.name);
                    }

                    if (!ok) {
                        std::lock_guard<std::mutex> lk(fail_m);
                        failed.push_back(bp + " :: " + fe.name);
                    }

                    int cur = ++done;
                    progress_update(cur, total,
                                    std::filesystem::path(fe.name)
                                        .filename().string());
                }
            }
        } catch (const std::exception& ex) {
            OutputLog::error(std::string(
                "BNK contents dump worker aborted: ") + ex.what());
            return;
        } catch (...) {
            OutputLog::error(
                "BNK contents dump worker aborted (unknown exception).");
            return;
        }

        if (S.cancel_requested.load()) {
            OutputLog::warn(std::string("BNK contents dump cancelled (")
                          + std::to_string(done.load()) + "/"
                          + std::to_string(total) + " written).");
            S.cancel_requested = false;
            return;
        }

        const int n_failed = (int)failed.size();
        if (n_failed > 0) {
            OutputLog::warn("BNK contents dump finished: " +
                            std::to_string(done.load() - n_failed) + "/" +
                            std::to_string(total) + " written, " +
                            std::to_string(n_failed) + " failed.");
        } else {
            OutputLog::success("BNK contents dump complete: " +
                               std::to_string(total) +
                               " file(s) written.");
        }
    }).detach();
}
