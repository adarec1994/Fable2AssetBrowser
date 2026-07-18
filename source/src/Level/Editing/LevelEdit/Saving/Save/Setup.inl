    if (!GameBackup::RequireBackup(msg)) return false;
    {
        FlatAssetEntry entry_copy;
        {
            std::lock_guard<std::mutex> lk(mtx());
            entry_copy = st().entry;
        }



        if (Level::Creation::IsCustomLooseLevel(entry_copy)) {
            return SaveWorkingCopy(msg);
        }
    }
    bool reload_needed = false;
    FlatAssetEntry reload_entry;
    bool need_bake = false;
    bool bake_iso = false;
    std::string bake_bnk_path;
    std::string bake_vpath;
    int bake_index = -1;
    size_t bake_count = 0;
    size_t render_placements_deleted = 0;
    std::vector<uint8_t> bake_bytes;
    int bake_ed_index = -1;
    std::vector<uint8_t> bake_ed_bytes;
    int bake_lmp_index = -1;
    std::vector<uint8_t> bake_lmp_bytes;
    int bake_lvstream_index = -1;
    std::vector<uint8_t> bake_lvstream_bytes;
    std::string bake_streaming_path;
    int bake_models_index = -1;
    std::vector<uint8_t> bake_models_bytes;
    std::vector<BnkWriter::EntryReplacement> bake_more;

    std::vector<uint8_t> gdb_rewrite_bytes;
    std::string gdb_rewrite_bnk;
    int gdb_rewrite_index = -1;
    std::string gdb_rewrite_loose;
    bool gdb_rewrite_iso = false;
    size_t contents_applied = 0;

    std::vector<uint8_t> save_rewrite_bytes;
    int save_rewrite_index = -1;
    std::string save_rewrite_bnk;
    size_t new_entities_created = 0;
    size_t generators_created = 0;
    size_t gdb_entities_deleted = 0;
    size_t spawn_points_deleted = 0;
    size_t save_entities_deleted = 0;
    size_t spawn_points_repaired = 0;
    size_t generators_repaired = 0;
    size_t save_physics_patched = 0;
    std::unordered_map<uint32_t, std::string> babel_edits;
    bool deferred_work = false;
