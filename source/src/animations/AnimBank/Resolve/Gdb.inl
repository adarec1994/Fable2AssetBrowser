size_t resolve_clip_names_from_gdb_animation_fields_for_root(
    const std::string& root,
    std::vector<AnimClip>& clips) {
    if (clips.empty()) return 0;

    g_model_animation_bindings.clear();

    std::unordered_map<uint32_t, size_t> by_key0;
    by_key0.reserve(clips.size());
    for (size_t i = 0; i < clips.size(); ++i) {
        by_key0.emplace(clips[i].key0, i);
    }

    GdbAnimScanStats stats;
    if (ISO::IsoMount::instance().is_mounted()) {
        for (const ISO::MountedFile& mf :
             ISO::IsoMount::instance().list_recursive(".gdb")) {
            auto bytes = ISO::IsoMount::instance().read_file(mf.path);
            if (bytes.empty()) {
                ++stats.files_seen;
                continue;
            }
            scan_gdb_animation_fields(bytes, by_key0, clips, stats);
        }
        for (const ISO::MountedFile& mf :
             ISO::IsoMount::instance().list_recursive(".bnk")) {
            scan_bnk_path_for_gdbs(
                ISO::IsoMount::make_iso_path(mf.path),
                by_key0, clips, stats);
        }
    } else {
        std::error_code ec;
        std::filesystem::path root_path(root);
        if (!std::filesystem::exists(root_path, ec)) return 0;
        for (auto it = std::filesystem::recursive_directory_iterator(
                 root_path,
                 std::filesystem::directory_options::skip_permission_denied,
                 ec);
             !ec && it != std::filesystem::recursive_directory_iterator();
             ++it) {
            std::string ext = it->path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) {
                               return static_cast<char>(std::tolower(c));
                           });
            if (!it->is_regular_file(ec) || ext != ".gdb") {
                continue;
            }

            std::ifstream f(it->path(), std::ios::binary | std::ios::ate);
            if (!f) {
                ++stats.files_seen;
                continue;
            }
            std::streamsize sz = f.tellg();
            f.seekg(0, std::ios::beg);
            std::vector<uint8_t> bytes((size_t)sz);
            if (!f.read(reinterpret_cast<char*>(bytes.data()), sz)) {
                ++stats.files_seen;
                continue;
            }
            scan_gdb_animation_fields(bytes, by_key0, clips, stats);
        }

        ec.clear();
        for (auto it = std::filesystem::recursive_directory_iterator(
                 root_path,
                 std::filesystem::directory_options::skip_permission_denied,
                 ec);
             !ec && it != std::filesystem::recursive_directory_iterator();
             ++it) {
            std::string ext = it->path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) {
                               return static_cast<char>(std::tolower(c));
                           });
            if (!it->is_regular_file(ec) || ext != ".bnk") {
                continue;
            }
            scan_bnk_path_for_gdbs(it->path().string(),
                                   by_key0, clips, stats);
        }
    }

    for (const GdbAnimScanStats::SelectorRecord& selector : stats.selectors) {
        auto rec = stats.animation_records.find(selector.selector_hash);
        if (rec == stats.animation_records.end()) continue;

        ++stats.selector_record_hits;
        AnimClip& clip = clips[rec->second.clip_index];
        if (is_default_clip_name(clip) || is_gdb_fallback_name(clip)) {
            clip.name = !selector.label.empty()
                ? selector.label
                : rec->second.label;
            ++stats.selector_names_assigned;
        }
    }

    char buf[512];
    std::snprintf(
        buf, sizeof(buf),
        "GDB animation scan: %zu/%zu GDB(s) parsed, %zu BNK(s) scanned "
        "(%zu nested, %zu GDB entry), %zu local + %zu inherited Animation "
        "field(s), %zu key hit(s) across %zu clip(s), %zu direct name(s) "
        "assigned; %zu named record ref(s), %zu named direct-key ref(s), "
        "%zu ref name(s) assigned; %zu AnimationName/AnimName selector "
        "field(s), %zu selector record hit(s), %zu selector name(s) "
        "assigned (%zu selector direct key hit(s)); %zu authored model "
        "animation binding(s) across %zu model hash(es).",
        stats.files_parsed, stats.files_seen,
        stats.bnks_seen, stats.bnk_nested_seen, stats.bnk_gdb_entries,
        stats.animation_fields, stats.inherited_animation_fields,
        stats.animation_key_hits,
        stats.unique_clip_hits.size(), stats.names_assigned,
        stats.reference_record_hits, stats.reference_key_hits,
        stats.reference_names_assigned,
        stats.selector_fields, stats.selector_record_hits,
        stats.selector_names_assigned, stats.selector_key_hits,
        stats.model_binding_refs, stats.model_binding_models.size());
    OutputLog::success(buf);

    ++g_model_animation_binding_revision;
    if (g_model_animation_binding_revision == 0) {
        g_model_animation_binding_revision = 1;
    }

    return stats.names_assigned + stats.reference_names_assigned +
           stats.selector_names_assigned;
}
