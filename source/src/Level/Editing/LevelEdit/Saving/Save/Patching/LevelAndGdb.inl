    std::string err;
    if (!lev_patches.empty()) {
        if (target_patchable_in_place(s.lev)) {
            for (const auto& p : lev_patches) {
                if ((uint64_t)p.off + (uint64_t)p.n * 4 >
                    s.lev.on_disk_size) continue;
                if (!patch_target(s.lev, p.off, p.v, p.n, err)) {
                    msg = "save failed (level file): " + err;
                    return false;
                }
                ++lev_written;
            }
            BnkCache::invalidate(s.lev.bnk_path);
        } else {
            std::vector<uint8_t> bytes;
            try {
                bytes = BnkCache::extract_bytes(s.lev.bnk_path,
                                                s.lev.file_index);
            } catch (...) { bytes.clear(); }
            if (bytes.empty()) {
                msg = "level re-extract failed";
                return false;
            }
            for (const auto& p : lev_patches) {
                if ((size_t)p.off + (size_t)p.n * 4 > bytes.size())
                    continue;
                for (int i = 0; i < p.n; ++i) {
                    put_f32_be(bytes.data() + p.off + i * 4, p.v[i]);
                }
                ++lev_written;
            }
            const auto out = edited_levels_dir() /
                std::filesystem::path(s.entry.full_path).filename();
            std::error_code ec;
            std::filesystem::create_directories(out.parent_path(), ec);
            std::ofstream f(out, std::ios::binary);
            if (!f) { msg = "could not write " + out.string(); return false; }
            f.write(reinterpret_cast<const char*>(bytes.data()),
                    (std::streamsize)bytes.size());
            OutputLog::warn("level edit: chunked level entry - patched "
                            "copy exported to " + out.string());
        }
    }
    if (!gdb_patches.empty()) {
        {
            char dbg[240];
            std::snprintf(
                dbg, sizeof(dbg),
                "gdb target: file='%s' bnk='%s' idx=%d valid=%d "
                "in_place=%d on_disk=%u patches=%zu",
                s.gdb.file_path.c_str(), s.gdb.bnk_path.c_str(),
                s.gdb.file_index, s.gdb.valid ? 1 : 0,
                target_patchable_in_place(s.gdb) ? 1 : 0,
                s.gdb.on_disk_size, gdb_patches.size());
            DebugLog::Write("save.gdb", dbg);
        }
        if (target_patchable_in_place(s.gdb)) {
            for (const auto& p : gdb_patches) {
                if (s.gdb.on_disk_size &&
                    (uint64_t)p.off + 4 > s.gdb.on_disk_size) continue;
                if (!patch_target(s.gdb, p.off, &p.v, 1, err)) {
                    DebugLog::Write("save.gdb",
                                    "patch FAILED off=" +
                                        std::to_string(p.off) + ": " +
                                        err);
                    msg = "save failed (.gdb): " + err;
                    return false;
                }
                {
                    char dbg[96];
                    std::snprintf(dbg, sizeof(dbg),
                                  "patched off=%u val=%.3f", p.off, p.v);
                    DebugLog::Write("save.gdb", dbg);
                }
                ++gdb_written;
            }
            if (!s.gdb.bnk_path.empty()) {
                BnkCache::invalidate(s.gdb.bnk_path);
            }
        } else if (s.gdb.valid) {
            std::vector<uint8_t> bytes;
            try {
                bytes = BnkCache::extract_bytes(s.gdb.bnk_path,
                                                s.gdb.file_index);
            } catch (...) { bytes.clear(); }
            if (!bytes.empty()) {
                for (const auto& p : gdb_patches) {
                    if ((size_t)p.off + 4 > bytes.size()) continue;
                    put_f32_be(bytes.data() + p.off, p.v);
                    ++gdb_written;
                }
                const auto out = edited_levels_dir() /
                    (std::filesystem::path(s.entry.full_path)
                         .stem().string() + ".gdb");
                std::error_code ec;
                std::filesystem::create_directories(out.parent_path(), ec);
                std::ofstream f(out, std::ios::binary);
                if (f) {
                    f.write(reinterpret_cast<const char*>(bytes.data()),
                            (std::streamsize)bytes.size());
                    OutputLog::warn("level edit: chunked .gdb entry - "
                                    "patched copy exported to " +
                                    out.string());
                }
            }
        } else {
            skipped += gdb_patches.size() / 3;
        }
    }
