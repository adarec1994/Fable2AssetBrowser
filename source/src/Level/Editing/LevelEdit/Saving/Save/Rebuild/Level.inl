    std::string berr;
    bool rebuilt = true;
    if (need_bake) {
    progress_update(10, 100, "Rebuilding level BNK...");
    BnkCache::invalidate(bake_bnk_path);
    if (bake_iso) {
        rebuilt = BnkWriter::RebuildIsoLevelBnk(bake_vpath, bake_index,
                                                bake_bytes, berr);
    } else {
        std::vector<BnkWriter::EntryReplacement> reps(1);
        reps[0].file_index = bake_index;
        reps[0].payload = std::move(bake_bytes);
        if (bake_ed_index >= 0 && !bake_ed_bytes.empty()) {
            BnkWriter::EntryReplacement r;
            r.file_index = bake_ed_index;
            r.payload = std::move(bake_ed_bytes);
            reps.push_back(std::move(r));
        }
        if (bake_lmp_index >= 0 && !bake_lmp_bytes.empty()) {
            BnkWriter::EntryReplacement r;
            r.file_index = bake_lmp_index;
            r.payload = std::move(bake_lmp_bytes);
            reps.push_back(std::move(r));
        }
        if (bake_lvstream_index >= 0 && !bake_lvstream_bytes.empty()) {
            BnkWriter::EntryReplacement r;
            r.file_index = bake_lvstream_index;
            r.payload = std::move(bake_lvstream_bytes);
            reps.push_back(std::move(r));
        }
        for (auto& r : bake_more) reps.push_back(std::move(r));
        rebuilt = BnkWriter::RebuildWithReplacedEntries(bake_bnk_path,
                                                        reps, berr);
        if (rebuilt && bake_models_index >= 0 &&
            !bake_models_bytes.empty()) {
            progress_update(75, 100, "Rebuilding streaming.bnk...");
            BnkCache::invalidate(bake_streaming_path);
            std::vector<BnkWriter::EntryReplacement> sreps(1);
            sreps[0].file_index = bake_models_index;
            sreps[0].payload = std::move(bake_models_bytes);
            rebuilt = BnkWriter::RebuildWithReplacedEntries(
                bake_streaming_path, sreps, berr);
            BnkCache::invalidate(bake_streaming_path);
            DebugTrace::log(
                "save: streaming.bnk models inject %s %s",
                rebuilt ? "OK" : "FAILED", berr.c_str());
        }
    }
    DebugTrace::log("save: bake %s (iso=%d target='%s') %s",
                    rebuilt ? "OK" : "FAILED", bake_iso ? 1 : 0,
                    bake_iso ? bake_vpath.c_str() : bake_bnk_path.c_str(),
                    berr.c_str());
    }
