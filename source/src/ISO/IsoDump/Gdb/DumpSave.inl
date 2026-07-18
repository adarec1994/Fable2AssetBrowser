void dump_gdb_save_files() {
    std::vector<std::string> all_bnks;
    all_bnks.reserve(S.bnk_paths.size() + S.nested_bnk_paths.size());
    all_bnks.insert(all_bnks.end(),
                    S.bnk_paths.begin(), S.bnk_paths.end());
    all_bnks.insert(all_bnks.end(),
                    S.nested_bnk_paths.begin(), S.nested_bnk_paths.end());

    if (all_bnks.empty()) {
        OutputLog::warn("Dump GDB/SAVE: no BNKs indexed (open a Fable 2 root first).");
        return;
    }

    const std::string export_root =
        S.export_dir.empty() ? std::filesystem::absolute("extracted").string()
                             : S.export_dir;

    OutputLog::info("Dumping every .gdb and .save with virtual paths -> " +
                    export_root);
    progress_open(0, "Dumping GDB/SAVE -> " + export_root);
    progress_update(0, 0, "Indexing...");

    std::thread([all_bnks = std::move(all_bnks), export_root]() {
        struct DumpGuard {
            ~DumpGuard() { progress_done(); }
        } pg;

        struct Target {
            std::string bnk_path;
            int         file_index = -1;
            std::string virtual_path;
            uint32_t    size = 0;
        };

        std::vector<Target> targets;
        for (const auto& bp : all_bnks) {
            if (S.cancel_requested.load() || S.exiting.load()) break;
            try {
                const auto ce = ::BnkCache::get(bp);
                const auto& files = ce.reader->list_files();
                const std::string nested_prefix =
                    nested_asset_prefix_for_bnk(bp);

                for (size_t i = 0; i < files.size(); ++i) {
                    std::string rel = nested_prefix + files[i].name;
                    while (!rel.empty() &&
                           (rel.front() == '/' || rel.front() == '\\')) {
                        rel.erase(rel.begin());
                    }

                    if (!ends_with_ci(rel, ".gdb") &&
                        !ends_with_ci(rel, ".save")) {
                        continue;
                    }

                    targets.push_back(Target{
                        bp, static_cast<int>(i), std::move(rel),
                        files[i].uncompressed_size
                    });
                }
            } catch (const std::exception& ex) {
                OutputLog::error("Dump GDB/SAVE: cannot index " + bp +
                                 ": " + ex.what());
            } catch (...) {
                OutputLog::error("Dump GDB/SAVE: cannot index " + bp);
            }
        }

        if (targets.empty()) {
            OutputLog::warn("Dump GDB/SAVE: no .gdb or .save files found.");
            return;
        }

        progress_update(0, static_cast<int>(targets.size()), "Starting...");

        const std::filesystem::path root(export_root);
        {
            std::error_code ec;
            std::filesystem::create_directories(root, ec);
        }

        std::ofstream manifest(root / "gdb_save_manifest.tsv",
                               std::ios::binary | std::ios::trunc);
        if (manifest) {
            manifest << "virtual_path\tbytes\tbnk_path\tfile_index\toutput_path\n";
        }

        std::unordered_set<std::string> seen_virtual_paths;
        seen_virtual_paths.reserve(targets.size() * 2);

        std::vector<std::string> failed;
        int written = 0;
        int duplicate_count = 0;

        for (size_t ti = 0; ti < targets.size(); ++ti) {
            if (S.cancel_requested.load() || S.exiting.load()) break;
            const Target& t = targets[ti];

            std::string key = t.virtual_path;
            std::replace(key.begin(), key.end(), '\\', '/');
            std::transform(key.begin(), key.end(), key.begin(),
                           [](unsigned char c) {
                               return static_cast<char>(std::tolower(c));
                           });

            std::filesystem::path out = root / t.virtual_path;
            if (!seen_virtual_paths.insert(key).second) {
                ++duplicate_count;
                const std::string bnk_stem =
                    std::filesystem::path(t.bnk_path).stem().string();
                out = root / "_gdb_save_duplicates" / bnk_stem /
                      t.virtual_path;
            }

            bool ok = false;
            size_t byte_count = t.size;
            try {
                auto bytes = ::BnkCache::extract_bytes(t.bnk_path,
                                                       t.file_index);
                byte_count = bytes.size();
                ok = write_buf_to_disk(out, bytes);
            } catch (const std::exception& ex) {
                OutputLog::error("Dump GDB/SAVE exception on " +
                                 t.virtual_path + ": " + ex.what());
            } catch (...) {
                OutputLog::error("Dump GDB/SAVE exception on " +
                                 t.virtual_path);
            }

            if (manifest) {
                manifest << t.virtual_path << '\t'
                         << byte_count << '\t'
                         << t.bnk_path << '\t'
                         << t.file_index << '\t'
                         << out.string() << '\n';
            }

            if (ok) {
                ++written;
            } else {
                failed.push_back(t.virtual_path);
            }

            progress_update(static_cast<int>(ti + 1),
                            static_cast<int>(targets.size()),
                            std::filesystem::path(t.virtual_path)
                                .filename().string());
        }

        if (S.cancel_requested.load()) {
            OutputLog::warn("Dump GDB/SAVE cancelled (" +
                            std::to_string(written) + "/" +
                            std::to_string(targets.size()) + " written).");
            S.cancel_requested = false;
            return;
        }

        if (!failed.empty()) {
            OutputLog::warn("Dump GDB/SAVE finished: " +
                            std::to_string(written) + "/" +
                            std::to_string(targets.size()) +
                            " written, " +
                            std::to_string(failed.size()) + " failed.");
        } else {
            OutputLog::success("Dump GDB/SAVE complete: " +
                               std::to_string(written) +
                               " file(s) written.");
        }

        if (duplicate_count > 0) {
            OutputLog::warn("Dump GDB/SAVE: " +
                            std::to_string(duplicate_count) +
                            " duplicate virtual path(s) written under "
                            "_gdb_save_duplicates.");
        }
    }).detach();
}
