void dump_tex_files_as(TexExportFormat fmt) {
    if (fmt == TexExportFormat::TEX) {
        dump_tex_files();
        return;
    }
    if (S.all_tex_files.empty()) {
        OutputLog::warn("Dump TEX as: no .tex files indexed (open a "
                        "Fable 2 root first).");
        return;
    }

    std::vector<FlatAssetEntry> targets = S.all_tex_files;
    const int total = (int)targets.size();

    const std::string export_root =
        S.export_dir.empty() ? std::filesystem::absolute("extracted").string()
                             : S.export_dir;
    const char* fmt_name =
        fmt == TexExportFormat::PNG  ? "PNG"  :
        fmt == TexExportFormat::JPG  ? "JPG"  :
        fmt == TexExportFormat::TIFF ? "TIFF" :
        fmt == TexExportFormat::DDS  ? "DDS"  : "?";

    OutputLog::info(std::string("Exporting ") + std::to_string(total) +
                    " .tex file(s) as " + fmt_name + " -> " +
                    export_root);
    progress_open(total,
                  std::string("Exporting TEXs as ") + fmt_name +
                  " -> " + export_root);
    progress_update(0, total, "Starting...");

    std::thread([targets = std::move(targets), total, fmt, fmt_name]() {
        struct PG {
            ~PG() { progress_done(); }
        } pg;

        std::atomic<int> done{0};
        std::vector<std::string> failed;
        std::mutex fail_m;

        for (const auto& e : targets) {
            if (S.cancel_requested.load() || S.exiting.load()) break;
            try {

                tex_export_begin_named(fmt, e.full_path, e.bnk_path,
                                       0);
            } catch (const std::exception& ex) {
                std::lock_guard<std::mutex> lk(fail_m);
                failed.push_back(e.full_path);
                OutputLog::error(std::string("TEX export exception (") +
                                 e.full_path + "): " + ex.what());
            } catch (...) {
                std::lock_guard<std::mutex> lk(fail_m);
                failed.push_back(e.full_path);
                OutputLog::error(std::string("TEX export exception (") +
                                 e.full_path + ")");
            }
            int cur = ++done;
            progress_update(cur, total,
                            std::filesystem::path(e.name)
                                .filename().string());
        }

        if (S.cancel_requested.load()) {
            OutputLog::warn(std::string("TEX export cancelled (") +
                            std::to_string(done.load()) + "/" +
                            std::to_string(total) + ").");
            S.cancel_requested = false;
            return;
        }
        const int n_failed = (int)failed.size();
        if (n_failed > 0) {
            OutputLog::warn(std::string("TEX export finished as ") +
                            fmt_name + ": " +
                            std::to_string(done.load() - n_failed) +
                            "/" + std::to_string(total) + " written, " +
                            std::to_string(n_failed) + " failed.");
        } else {
            OutputLog::success(std::string("TEX export complete as ") +
                               fmt_name + ": " +
                               std::to_string(total) + " files written.");
        }
    }).detach();
}
