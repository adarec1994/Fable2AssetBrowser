void mdl_export_begin_named(MdlExportFormat fmt,
                            const std::string& bnk_path,
                            int file_index,
                            const std::string& display_path,
                            bool )
{
    if (bnk_path.empty() || file_index < 0) {
        OutputLog::error("MDL export: missing bnk / index arg.");
        return;
    }

    std::string entry_name;
    try {
        BNKReader r(bnk_path);
        const auto& files = r.list_files();
        if ((size_t)file_index < files.size()) {
            entry_name = files[file_index].name;
        }
    } catch (...) {  }

    std::string out_rel;
    if (!display_path.empty() && display_path.find('/') != std::string::npos) {
        out_rel = display_path;
    } else if (!entry_name.empty()) {
        out_rel = entry_name;
    } else if (!display_path.empty()) {
        out_rel = display_path;
    } else {
        out_rel = std::string("model_") + std::to_string(file_index) + ".mdl";
    }
    std::string log_label = display_path.empty()
        ? std::filesystem::path(out_rel).filename().string()
        : std::filesystem::path(display_path).filename().string();

    auto buf = reconstruct_one_mdl_for_export(bnk_path, file_index);
    if (buf.empty()) {
        OutputLog::error(std::string("MDL export: rebuild failed for ")
                         + log_label);
        return;
    }

    std::string rel = out_rel;
    while (!rel.empty() && (rel.front() == '/' || rel.front() == '\\'))
        rel.erase(rel.begin());
    std::filesystem::path root =
        S.export_dir.empty() ? std::filesystem::path("extracted")
                             : std::filesystem::path(S.export_dir);
    auto out = root / std::filesystem::path(rel);
    out.replace_extension(mdl_fmt_ext(fmt));

    std::error_code ec;
    if (auto parent = out.parent_path(); !parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            OutputLog::error(std::string("MDL export: cannot create ") +
                             parent.string() + " - " + ec.message());
            return;
        }
    }

    bool ok = false;
    std::string err;
    try {
        switch (fmt) {
            case MdlExportFormat::GLB:
                ok = mdl_to_glb_full(buf, out.string(), out_rel, err);
                break;
            case MdlExportFormat::FBX:
                ok = mdl_to_fbx_full(buf, out.string(), out_rel, err);
                break;
            case MdlExportFormat::RAW: {
                std::ofstream f(out, std::ios::binary | std::ios::trunc);
                if (f) {
                    f.write((const char*)buf.data(),
                            (std::streamsize)buf.size());
                    ok = f.good();
                } else err = "cannot open output for writing";
                break;
            }
        }
    } catch (const std::exception& ex) {
        err = ex.what();
    } catch (...) {
        err = "unknown exception";
    }

    if (ok) {
        OutputLog::success(std::string("Exported ") + log_label +
                           " as " + mdl_fmt_label(fmt) + " -> " +
                           out.string());
    } else {
        OutputLog::error(std::string("MDL export failed (") +
                         mdl_fmt_label(fmt) + "): " + log_label +
                         (err.empty() ? "" : " - " + err));
    }
}

void dump_mdl_files_as(MdlExportFormat fmt) {
    if (fmt == MdlExportFormat::RAW) {

        dump_mdl_files();
        return;
    }
    if (S.all_mdl_files.empty()) {
        OutputLog::warn("Dump MDL: no .mdl files indexed (open a "
                        "Fable 2 root first).");
        return;
    }

    std::vector<FlatAssetEntry> targets = S.all_mdl_files;
    const int total = (int)targets.size();
    const std::string export_root =
        S.export_dir.empty() ? std::filesystem::absolute("extracted").string()
                             : S.export_dir;

    OutputLog::info(std::string("Exporting ") + std::to_string(total) +
                    " .mdl file(s) as " + mdl_fmt_label(fmt) + " -> " +
                    export_root);
    progress_open(total,
                  std::string("Exporting MDLs as ") + mdl_fmt_label(fmt) +
                  " -> " + export_root);
    progress_update(0, total, "Starting...");

    std::thread([targets = std::move(targets), total, fmt]() {
        struct PG { ~PG() { progress_done(); } } pg;
        std::atomic<int> done{0};
        std::vector<std::string> failed;
        std::mutex fail_m;

        for (const auto& e : targets) {
            if (S.cancel_requested.load() || S.exiting.load()) break;
            try {
                mdl_export_begin_named(fmt, e.bnk_path, e.file_index,
                                       e.full_path, e.from_nested);
            } catch (...) {
                std::lock_guard<std::mutex> lk(fail_m);
                failed.push_back(e.full_path);
            }
            int cur = ++done;
            progress_update(cur, total,
                            std::filesystem::path(e.name)
                                .filename().string());
        }

        if (S.cancel_requested.load()) {
            OutputLog::warn(std::string("MDL export cancelled (") +
                            std::to_string(done.load()) + "/" +
                            std::to_string(total) + ").");
            S.cancel_requested = false;
            return;
        }
        const int n_failed = (int)failed.size();
        if (n_failed > 0) {
            OutputLog::warn(std::string("MDL export finished as ") +
                            mdl_fmt_label(fmt) + ": " +
                            std::to_string(done.load() - n_failed) +
                            "/" + std::to_string(total) + " written, " +
                            std::to_string(n_failed) + " failed.");
        } else {
            OutputLog::success(std::string("MDL export complete as ") +
                               mdl_fmt_label(fmt) + ": " +
                               std::to_string(total) + " files written.");
        }
    }).detach();
}
