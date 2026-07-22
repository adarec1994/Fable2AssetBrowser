    bool contents_ok = true;
    if (rebuilt && !gdb_rewrite_bytes.empty()) {
        progress_update(85, 100, "Writing container data...");
        std::string gerr;
        {
            char dbg[200];
            std::snprintf(dbg, sizeof(dbg),
                          "writing rewritten gdb (%zu bytes) -> %s",
                          gdb_rewrite_bytes.size(),
                          !gdb_rewrite_loose.empty()
                              ? gdb_rewrite_loose.c_str()
                              : gdb_rewrite_bnk.c_str());
            DebugLog::Write("save.rebuild", dbg);
        }
        if (!gdb_rewrite_loose.empty()) {
            std::ofstream f(gdb_rewrite_loose,
                            std::ios::binary | std::ios::trunc);
            contents_ok = bool(f);
            if (contents_ok) {
                f.write(reinterpret_cast<const char*>(
                            gdb_rewrite_bytes.data()),
                        std::streamsize(gdb_rewrite_bytes.size()));
                contents_ok = f.good();
            }
            if (!contents_ok) gerr = "loose .gdb write failed";
            if (contents_ok && save_rewrite_index >= 0 &&
                !save_rewrite_bnk.empty()) {
                BnkCache::invalidate(save_rewrite_bnk);
                contents_ok = BnkWriter::RebuildWithReplacedEntry(
                    save_rewrite_bnk, save_rewrite_index,
                    save_rewrite_bytes, gerr);
                BnkCache::invalidate(save_rewrite_bnk);
            }
        } else if (gdb_rewrite_iso) {
            contents_ok = BnkWriter::RebuildIsoLevelBnk(
                ISO::IsoMount::strip_iso_prefix(gdb_rewrite_bnk),
                gdb_rewrite_index, gdb_rewrite_bytes, gerr);
            if (contents_ok && save_rewrite_index >= 0) {
                contents_ok = BnkWriter::RebuildIsoLevelBnk(
                    ISO::IsoMount::strip_iso_prefix(save_rewrite_bnk),
                    save_rewrite_index, save_rewrite_bytes, gerr);
            }
        } else if (gdb_rewrite_index >= 0) {
            BnkCache::invalidate(gdb_rewrite_bnk);
            std::vector<BnkWriter::EntryReplacement> reps(1);
            reps[0].file_index = gdb_rewrite_index;
            reps[0].payload = std::move(gdb_rewrite_bytes);
            const bool save_same_bnk =
                save_rewrite_index >= 0 &&
                save_rewrite_bnk == gdb_rewrite_bnk;
            if (save_same_bnk) {
                BnkWriter::EntryReplacement r;
                r.file_index = save_rewrite_index;
                r.payload = std::move(save_rewrite_bytes);
                reps.push_back(std::move(r));
            }
            contents_ok = BnkWriter::RebuildWithReplacedEntries(
                gdb_rewrite_bnk, reps, gerr);
            BnkCache::invalidate(gdb_rewrite_bnk);
            if (contents_ok && save_rewrite_index >= 0 && !save_same_bnk) {
                BnkCache::invalidate(save_rewrite_bnk);
                contents_ok = BnkWriter::RebuildWithReplacedEntry(
                    save_rewrite_bnk, save_rewrite_index,
                    save_rewrite_bytes, gerr);
                BnkCache::invalidate(save_rewrite_bnk);
            }
        } else {
            contents_ok = false;
            gerr = "no .gdb target";
        }
    }

    if (rebuilt && contents_ok && gdb_rewrite_bytes.empty() &&
        save_rewrite_index >= 0 && !save_rewrite_bytes.empty()) {
        progress_update(85, 100, "Writing entity save data...");
        std::string gerr;
        BnkCache::invalidate(save_rewrite_bnk);
        if (ISO::IsoMount::is_iso_path(save_rewrite_bnk)) {
            contents_ok = BnkWriter::RebuildIsoLevelBnk(
                ISO::IsoMount::strip_iso_prefix(save_rewrite_bnk),
                save_rewrite_index, save_rewrite_bytes, gerr);
        } else {
            contents_ok = BnkWriter::RebuildWithReplacedEntry(
                save_rewrite_bnk, save_rewrite_index, save_rewrite_bytes,
                gerr);
        }
        BnkCache::invalidate(save_rewrite_bnk);
    }
