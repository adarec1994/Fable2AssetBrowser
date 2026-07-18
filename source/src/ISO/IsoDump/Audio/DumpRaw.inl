void dump_wav_files() {
    if (S.all_wav_files.empty()) {
        OutputLog::warn("Dump WAV: no .wav files indexed (open a "
                        "Fable 2 root first).");
        return;
    }

    std::vector<FlatAssetEntry> targets = S.all_wav_files;

    std::unordered_map<std::string, std::vector<int>> by_bnk;
    by_bnk.reserve(64);
    for (size_t i = 0; i < targets.size(); ++i) {
        by_bnk[targets[i].bnk_path].push_back((int)i);
    }
    const int total = (int)targets.size();

    const std::string export_root =
        S.export_dir.empty() ? std::filesystem::absolute("extracted").string()
                             : S.export_dir;

    OutputLog::info(std::string("Dumping ") + std::to_string(total) +
                    " .wav file(s) from " +
                    std::to_string(by_bnk.size()) + " BNK(s) -> " +
                    export_root);
    progress_open(total, std::string("Dumping WAVs -> ") + export_root);
    progress_update(0, total, "Starting...");

    std::thread([targets = std::move(targets),
                 by_bnk = std::move(by_bnk),
                 total]() {
        struct DumpGuard {
            ~DumpGuard() { progress_done(); }
        } pg;

        std::atomic<int> done{0};
        std::vector<std::string> failed;
        std::mutex fail_m;

        std::unordered_map<std::string, BnkCacheEntry> bnk_cache;

        try {
            for (const auto& [bnk_path, indices] : by_bnk) {
                if (S.cancel_requested.load() || S.exiting.load()) break;

                BnkCacheEntry* body_ce = get_or_open_bnk(bnk_cache, bnk_path);
                if (!body_ce) {
                    OutputLog::error(std::string("WAV dump: cannot open ")
                                     + bnk_path);
                    for (int ti : indices) {
                        std::lock_guard<std::mutex> lk(fail_m);
                        failed.push_back(targets[(size_t)ti].full_path);
                        ++done;
                    }
                    progress_update(done.load(), total,
                                    std::filesystem::path(bnk_path)
                                        .filename().string());
                    continue;
                }

                for (int ti : indices) {
                    if (S.cancel_requested.load() || S.exiting.load()) break;
                    const auto& e = targets[(size_t)ti];

                    auto out = build_asset_out_path(e, ".wav");
                    bool ok = false;
                    try {
                        std::error_code ec;
                        if (auto parent = out.parent_path(); !parent.empty()) {
                            std::filesystem::create_directories(parent, ec);
                        }

                        extract_one(bnk_path, e.file_index, out.string());
                        ok = std::filesystem::exists(out, ec) && !ec;
                    } catch (const std::exception& ex) {
                        OutputLog::error(std::string("WAV exception on ") +
                                         e.full_path + ": " + ex.what());
                    } catch (...) {
                        OutputLog::error(std::string("WAV exception on ") +
                                         e.full_path);
                    }

                    if (!ok) {
                        OutputLog::error(std::string("WAV write failed: ") +
                                         e.full_path);
                        std::lock_guard<std::mutex> lk(fail_m);
                        failed.push_back(e.full_path);
                    }

                    int cur = ++done;
                    progress_update(cur, total,
                                    std::filesystem::path(e.name)
                                        .filename().string());
                }
            }
        } catch (const std::exception& ex) {
            OutputLog::error(std::string("WAV dump worker aborted: ") +
                             ex.what());
            return;
        } catch (...) {
            OutputLog::error("WAV dump worker aborted (unknown exception).");
            return;
        }

        if (S.cancel_requested.load()) {
            OutputLog::warn(std::string("WAV dump cancelled (")
                          + std::to_string(done.load()) + "/"
                          + std::to_string(total) + " written).");
            S.cancel_requested = false;
            return;
        }

        const int n_failed = (int)failed.size();
        if (n_failed > 0) {
            OutputLog::warn("WAV dump finished: " +
                            std::to_string(done.load() - n_failed) + "/" +
                            std::to_string(total) + " written, " +
                            std::to_string(n_failed) + " failed.");
        } else {
            OutputLog::success("WAV dump complete: " +
                               std::to_string(total) + " files written.");
        }
    }).detach();
}
