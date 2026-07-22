int SilverKeyChestRequirement(const std::string& model_path)
{
    std::string leaf = model_path;
    const size_t slash = leaf.find_last_of("/\\");
    if (slash != std::string::npos) leaf.erase(0, slash + 1);

    std::string compact;
    compact.reserve(leaf.size());
    for (unsigned char c : leaf) {
        if (std::isalnum(c)) {
            compact.push_back(char(std::tolower(c)));
        }
    }
    constexpr const char* marker = "silverkeychest";
    const size_t marker_pos = compact.find(marker);
    if (marker_pos == std::string::npos) return 0;

    size_t p = marker_pos + std::strlen(marker);
    int required = 0;
    while (p < compact.size() && std::isdigit(
               static_cast<unsigned char>(compact[p]))) {
        required = std::min(999, required * 10 + (compact[p] - '0'));
        ++p;
    }
    return required > 0 ? required : 1;
}

void OnLevelLoaded(const FlatAssetEntry& entry) {
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    const bool same_level = s.available &&
                            s.entry.full_path == entry.full_path &&
                            s.entry.bnk_path == entry.bnk_path;
    ModuleState kept{};
    bool carry = false;
    {
        char dbg[160];
        std::snprintf(dbg, sizeof(dbg),
                      "OnLevelLoaded '%s' same_level=%d dirty=%d "
                      "edits=%zu",
                      entry.name.c_str(), same_level ? 1 : 0,
                      s.dirty ? 1 : 0, s.edits.size());
        DebugLog::Write("pin", dbg);
    }
    if (s.dirty) {
        if (same_level) {
            carry = true;
            kept.enabled = s.enabled;
            kept.edits = std::move(s.edits);
            kept.undo_stack = std::move(s.undo_stack);
            kept.contents_edits = std::move(s.contents_edits);
            kept.contents_loot_edits = std::move(s.contents_loot_edits);
            kept.text_edits = std::move(s.text_edits);
            kept.generators = std::move(s.generators);
            kept.spawn_point_adds = std::move(s.spawn_point_adds);
            kept.spawn_point_deletes = std::move(s.spawn_point_deletes);
            OutputLog::info(
                "level edit: unsaved edits carried across reload");
        } else {
            OutputLog::warn("level edit: unsaved object edits discarded "
                            "(level reloaded)");
        }
    }
    const uint64_t rev = s.revision;
    s = ModuleState{};
    s.revision = rev + 1;
    s.entry = entry;
    s.available = true;
    s.lev.bnk_path = entry.bnk_path;
    s.lev.file_index = entry.file_index;
    std::error_code loose_ec;
    if (std::filesystem::is_directory(entry.bnk_path, loose_ec)) {
        
        
        const std::filesystem::path loose =
            std::filesystem::path(entry.bnk_path) / entry.full_path;
        if (std::filesystem::is_regular_file(loose, loose_ec)) {
            s.lev.file_path = loose.string();
            s.lev.on_disk_size =
                (uint32_t)std::filesystem::file_size(loose, loose_ec);
            s.lev.valid = s.lev.on_disk_size != 0;
        }
    } else {
        fill_bnk_target(s.lev);
    }
    load_additions(s);
    load_spawns(s);
    if (carry) {
        s.enabled = kept.enabled;
        s.edits = std::move(kept.edits);
        s.undo_stack = std::move(kept.undo_stack);
        s.contents_edits = std::move(kept.contents_edits);
        s.contents_loot_edits = std::move(kept.contents_loot_edits);
        s.text_edits = std::move(kept.text_edits);
        s.generators = std::move(kept.generators);
        s.spawn_point_adds = std::move(kept.spawn_point_adds);
        s.spawn_point_deletes = std::move(kept.spawn_point_deletes);
        s.dirty = true;
    }
    OutputLog::info(
        "level edit: tracking '" + entry.name + "' (" +
        (s.lev.compressed ? "chunked" : "raw") + " entry, slot " +
        std::to_string(s.lev.on_disk_size) + " B" +
        (s.lev.in_iso ? ", ISO-hosted)" : ")"));
}

void SetGdbSource(const std::string& bnk_path,
                  int file_index,
                  const std::string& loose_file) {
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    s.gdb = FileTarget{};
    if (!loose_file.empty()) {
        s.gdb.file_path = loose_file;
        s.gdb.valid = true;
    } else {
        s.gdb.bnk_path = bnk_path;
        s.gdb.file_index = file_index;
        fill_bnk_target(s.gdb);
    }
    if (s.gdb.valid) {
        OutputLog::info(
            "level edit: gdb source " +
            (!s.gdb.file_path.empty()
                 ? s.gdb.file_path
                 : std::filesystem::path(s.gdb.bnk_path).filename()
                           .string() +
                       "#" + std::to_string(s.gdb.file_index) + " (" +
                       (s.gdb.compressed ? "chunked" : "raw") + ")"));
    }
}

bool Available() {
    std::lock_guard<std::mutex> lk(mtx());
    return st().available;
}
bool Enabled() {
    std::lock_guard<std::mutex> lk(mtx());
    return st().available && st().enabled;
}
bool Dirty() {
    std::lock_guard<std::mutex> lk(mtx());
    return st().dirty;
}

bool Saving() {
    std::lock_guard<std::mutex> lk(mtx());
    return st().saving;
}

size_t EditedCount() {
    std::lock_guard<std::mutex> lk(mtx());
    size_t n = 0;
    for (const auto& kv : st().edits) {
        if (kv.second.changed()) ++n;
    }
    return n;
}

uint64_t Revision() {
    std::lock_guard<std::mutex> lk(mtx());
    return st().revision;
}
