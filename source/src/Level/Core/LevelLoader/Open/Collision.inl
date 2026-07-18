    for (const auto& vfs_stream_path : g_level_vfs_streaming_bnks) {
        std::string wanted_leaf =
            std::filesystem::path(vfs_stream_path).filename().string();
        std::transform(wanted_leaf.begin(), wanted_leaf.end(),
                       wanted_leaf.begin(), ::tolower);

        auto leaf_matches = [&](const std::string& mounted_leaf_lower) {
            if (mounted_leaf_lower == wanted_leaf) return true;
            if (mounted_leaf_lower.size() <= wanted_leaf.size() + 1) return false;
            const size_t off = mounted_leaf_lower.size() - wanted_leaf.size();
            if (mounted_leaf_lower.compare(off, wanted_leaf.size(),
                                           wanted_leaf) != 0) return false;
            return mounted_leaf_lower[off - 1] == '_';
        };

        std::string mounted_path;
        if (auto resolved = find_bnk_by_virtual_path(vfs_stream_path)) {
            mounted_path = *resolved;
        }
        if (mounted_path.empty()) {
            for (const auto& p : S.bnk_paths) {
                std::string leaf =
                    std::filesystem::path(p).filename().string();
                std::transform(leaf.begin(), leaf.end(), leaf.begin(), ::tolower);
                if (leaf_matches(leaf)) { mounted_path = p; break; }
            }
        }
        if (mounted_path.empty()) {
            for (const auto& p : S.nested_bnk_paths) {
                std::string leaf =
                    std::filesystem::path(p).filename().string();
                std::transform(leaf.begin(), leaf.end(),
                               leaf.begin(), ::tolower);
                if (leaf_matches(leaf)) { mounted_path = p; break; }
            }
        }
        if (mounted_path.empty()) {
            OutputLog::warn("streaming bnk not mounted: " + vfs_stream_path);
            continue;
        }

        try {
            const BnkCache::Entry bnk = BnkCache::get(mounted_path);
            const auto& files = bnk.reader->list_files();
            size_t hkx_count = 0;
            size_t total_rb  = 0;
            size_t total_inst = 0;

            for (size_t i = 0; i < files.size(); ++i) {
                const auto& name = files[i].name;
                std::string lower = name;
                std::transform(lower.begin(), lower.end(),
                               lower.begin(), ::tolower);

                if (lower.size() < 4 ||
                    lower.compare(lower.size() - 4, 4, ".hkx") != 0) continue;
                ++hkx_count;
                std::vector<uint8_t> hkx_bytes;
                try {
                    hkx_bytes = bnk.reader->extract_index_bytes((int)i);
                } catch (...) { continue; }
                auto pf = Havok::LoadPackFileFromBytes(
                    std::move(hkx_bytes), name);
                if (!pf) continue;
                total_inst += pf->virtual_fixups.size();
                const auto* rb_class = pf->find_class("hkpRigidBody");
                size_t this_rb = 0;
                if (rb_class) {
                    for (const auto& vf : pf->virtual_fixups) {
                        if (vf.classnames_offset ==
                            rb_class->classnames_offset) {
                            ++total_rb;
                            ++this_rb;
                        }
                    }
                }

            }

            std::ostringstream os;
            os << "streaming bnk '"
               << std::filesystem::path(mounted_path).filename().string()
               << "':  " << files.size() << " files, " << hkx_count
               << " .hkx,  " << total_rb << " rigid bodies across "
               << total_inst << " havok instances";
            OutputLog::success(os.str());

        } catch (const std::exception& ex) {
            OutputLog::warn(std::string("streaming bnk scan failed: ") + ex.what());
        }
    }

