static bool prepare_replacement(const ImageLoad::Image& decoded,
                                const std::string& target_bnk_path,
                                int target_file_index, const Options& opt,
                                PreparedReplacement& prepared,
                                std::string& err) {
    prepared = PreparedReplacement{};
    std::vector<TextureTargets>& groups = prepared.groups;
    std::vector<TexturePart>& shared_mip0 = prepared.shared_mip0;
    if (!resolve_all_texture_targets(target_bnk_path, target_file_index,
                                     groups, shared_mip0, err)) {
        return false;
    }
    const TextureTargets& target = groups.front();
    {
        size_t headers = 0, bodies = 0;
        for (const TextureTargets& g : groups) {
            if (g.header.index >= 0) ++headers;
            if (g.body.index >= 0) ++bodies;
        }
        DebugLog::Write("texture-replace",
            target.virtual_path + " | copies: " + std::to_string(headers) +
            " header(s), " + std::to_string(bodies) + " body(ies), " +
            std::to_string(shared_mip0.size()) + " shared mip0 entr(ies)");
    }

    TexWriter::Options texture_options;
    texture_options.max_dimension = opt.max_tex_dim;
    texture_options.generate_mips = opt.generate_mips;
    texture_options.format = opt.tex_format;
    texture_options.force_mip0_split = !shared_mip0.empty();
    if (!shared_mip0.empty()) texture_options.generate_mips = true;

    const std::string key = normalized_path(target.virtual_path);
    auto backup_counterpart = [](const std::string& live) -> std::string {
        if (S.root_dir.empty() || GameBackup::IsBackupPath(live)) {
            return {};
        }
        std::error_code ec;
        const std::filesystem::path rel =
            std::filesystem::relative(live, S.root_dir, ec);
        if (ec || rel.empty() || rel.generic_string().rfind("..", 0) == 0) {
            return {};
        }
        const std::filesystem::path p =
            std::filesystem::path(S.root_dir) / "f2ab_backup" / rel;
        return std::filesystem::is_regular_file(p, ec) ? p.string()
                                                       : std::string();
    };
    auto extract_part_original = [&](const TexturePart& part,
                                     bool& from_backup)
        -> std::vector<uint8_t> {
        from_backup = false;
        if (part.index < 0 || part.path.empty()) return {};
        const std::string backup = backup_counterpart(part.path);
        if (!backup.empty()) {
            try {
                const int bidx = BnkCache::find_index(backup, key);
                if (bidx >= 0) {
                    from_backup = true;
                    return BnkCache::extract_bytes(backup, bidx);
                }
            } catch (...) {
            }
        }
        try {
            return BnkCache::extract_bytes(part.path, part.index);
        } catch (...) {
        }
        return {};
    };

    bool header_from_backup = false;
    bool mip0_from_backup = false;
    bool body_from_backup = false;
    const std::vector<uint8_t> header_orig =
        extract_part_original(target.header, header_from_backup);
    const std::vector<uint8_t> body_orig =
        extract_part_original(target.body, body_from_backup);
    std::vector<uint8_t> mip0_orig;
    for (const TexturePart& part : shared_mip0) {
        mip0_orig = extract_part_original(part, mip0_from_backup);
        if (!mip0_orig.empty()) break;
    }
    const std::string mip0_src =
        mip0_from_backup ? "pristine backup" : "live bank";

    uint32_t orig_format = 0, orig_w = 0, orig_h = 0, orig_mips = 0;
    if (header_orig.size() >= 32) {
        auto be = [&](size_t o) {
            return (uint32_t(header_orig[o]) << 24) |
                   (uint32_t(header_orig[o + 1]) << 16) |
                   (uint32_t(header_orig[o + 2]) << 8) |
                   uint32_t(header_orig[o + 3]);
        };
        orig_w = be(16);
        orig_h = be(20);
        orig_format = be(24);
        orig_mips = be(28);
    }

    uint32_t mip0_cf = 0;
    if (mip0_orig.size() >= 4) {
        mip0_cf = (uint32_t(mip0_orig[0]) << 24) |
                  (uint32_t(mip0_orig[1]) << 16) |
                  (uint32_t(mip0_orig[2]) << 8) | uint32_t(mip0_orig[3]);
    }

    if (!shared_mip0.empty()) {
        uint32_t true_w = 0, true_h = 0;
        if (mip0_orig.size() >= 48) {
            auto be32 = [&](size_t o) {
                return (uint32_t(mip0_orig[o]) << 24) |
                       (uint32_t(mip0_orig[o + 1]) << 16) |
                       (uint32_t(mip0_orig[o + 2]) << 8) |
                       uint32_t(mip0_orig[o + 3]);
            };
            const uint32_t cf = be32(0);
            const uint32_t ds = be32(8);
            if ((cf == 1 || cf == 2 || cf == 3 || cf == 4 || cf == 11) &&
                mip0_orig.size() >= 52) {
                true_w = (uint32_t(mip0_orig[50]) << 8) | mip0_orig[51];
                true_h = (uint32_t(mip0_orig[48]) << 8) | mip0_orig[49];
            } else if (cf == 7 && ds > 0) {
                uint64_t pixels = 0;
                if (orig_format == 35) pixels = (uint64_t)ds * 2;
                else if (orig_format == 39 || orig_format == 40) pixels = ds;
                else if (orig_format == 2 || orig_format == 4) pixels = ds / 4;
                uint32_t dim = 1;
                while (dim < 8192 && (uint64_t)dim * dim < pixels) dim <<= 1;
                if (pixels && (uint64_t)dim * dim == pixels) {
                    true_w = dim;
                    true_h = dim;
                }
            }
        }
        auto pow2 = [](uint32_t v) { return v && (v & (v - 1)) == 0; };
        if (true_w >= 4 && true_h >= 4 && true_w <= 4096 && true_h <= 4096 &&
            pow2(true_w) && pow2(true_h)) {
            if (true_w != orig_w || true_h != orig_h) {
                uint32_t chain = 1;
                for (uint32_t d = std::max(true_w, true_h); d > 1; d >>= 1) {
                    ++chain;
                }
                DebugLog::Write("texture-replace",
                    "header says " + std::to_string(orig_w) + "x" +
                    std::to_string(orig_h) + " mips=" +
                    std::to_string(orig_mips) +
                    " but the streamed mip0 is really " +
                    std::to_string(true_w) + "x" + std::to_string(true_h) +
                    " (" + mip0_src + "); rebuilding the full chain");
                orig_w = true_w;
                orig_h = true_h;
                orig_mips = chain;
            }
        } else if (!mip0_orig.empty()) {
            DebugLog::Write("texture-replace",
                "warning: could not derive the streamed mip0's true size (" +
                mip0_src + "); trusting the header geometry");
        }
    }

    if (texture_options.format == TexWriter::Format::Auto) {
        if (orig_format == 35) {
            texture_options.format = TexWriter::Format::BC1;
        } else if (orig_format == 39) {
            texture_options.format = TexWriter::Format::BC3;
        } else if (orig_format == 40) {
            texture_options.format = TexWriter::Format::BC5Normal;
        } else if (orig_format == 2 || orig_format == 4) {
            texture_options.format = TexWriter::Format::RawARGB;
        }
    }

    if (orig_w > 0 && orig_h > 0 && orig_mips > 0 &&
        orig_w <= 4096 && orig_h <= 4096 && orig_mips <= 14) {
        texture_options.match_width = (int)orig_w;
        texture_options.match_height = (int)orig_h;
        texture_options.match_mip_count = (int)orig_mips;
        DebugLog::Write("texture-replace",
            "matching original geometry " + std::to_string(orig_w) + "x" +
            std::to_string(orig_h) + " mips=" + std::to_string(orig_mips) +
            " pf=" + std::to_string(orig_format));
    } else if (!shared_mip0.empty()) {
        err = "could not read the original texture's dimensions to rebuild "
              "its streamed 1024 mip; aborting to avoid crashing the game";
        return false;
    }

    if (texture_options.match_mip_count > 0) {
        std::vector<uint32_t> flags;
        bool mirrored = false;
        std::vector<uint8_t> whole;
        whole.reserve(header_orig.size() + mip0_orig.size() +
                      body_orig.size());
        whole.insert(whole.end(), header_orig.begin(), header_orig.end());
        whole.insert(whole.end(), mip0_orig.begin(), mip0_orig.end());
        whole.insert(whole.end(), body_orig.begin(), body_orig.end());
        TexInfo ti;
        if (!header_orig.empty() && parse_tex_info(whole, ti) &&
            ti.Mips.size() == (size_t)orig_mips &&
            ti.TextureWidth == orig_w && ti.TextureHeight == orig_h) {
            const uint32_t mirror_top = ti.Mips.front().CompFlag;
            const bool mip0_disagrees = mip0_from_backup &&
                                        mip0_cf != 0 &&
                                        mip0_cf != mirror_top;
            if (!mip0_disagrees) {
                for (const auto& mip : ti.Mips) {
                    flags.push_back(mip.CompFlag);
                }
                mirrored = true;
            }
        }
        if (!mirrored && (mip0_cf == 1 || mip0_cf == 11)) {
            uint32_t w = orig_w;
            uint32_t h = orig_h;
            for (uint32_t i = 0; i < orig_mips; ++i) {
                flags.push_back((w >= 64 && h >= 64) ? mip0_cf : 7u);
                w = std::max(1u, w >> 1);
                h = std::max(1u, h >> 1);
            }
        }
        if (!mirrored &&
            (mip0_cf == 2 || mip0_cf == 3 || mip0_cf == 4)) {
            uint32_t w = orig_w;
            uint32_t h = orig_h;
            for (uint32_t i = 0; i < orig_mips; ++i) {
                flags.push_back((w >= 32 && h >= 32) ? mip0_cf : 7u);
                w = std::max(1u, w >> 1);
                h = std::max(1u, h >> 1);
            }
        }
        if (!flags.empty()) {
            std::string summary;
            bool variant_codec = false;
            for (uint32_t flag : flags) {
                if (!summary.empty()) summary += ",";
                summary += std::to_string(flag);
                if (flag == 2 || flag == 3 || flag == 4) {
                    variant_codec = true;
                }
            }
            DebugLog::Write("texture-replace",
                std::string(mirrored
                    ? "mirroring original chunk flags ["
                    : "rebuilding chunk flags from the pristine mip0 [") +
                summary + "]");
            if (variant_codec &&
                texture_options.format != TexWriter::Format::BC5Normal) {
                DebugLog::Write("texture-replace",
                    "warning: original uses the cf=2/3/4 variant codec but "
                    "the texture is not BC5; those chunks fall back to raw "
                    "cf=7");
            }
            texture_options.match_comp_flags = std::move(flags);
        }
    }

    progress_update(40, 100, "Encoding " + target.virtual_path);
    if (!TexWriter::build_from_rgba(
            decoded.rgba.data(), decoded.width, decoded.height,
            texture_options, prepared.built, err)) {
        return false;
    }
    if (!verify_tex(prepared.built, err)) {
        return false;
    }
    return true;
}

bool replace_texture(const std::string& img_path,
                     const std::string& target_bnk_path,
                     int target_file_index, const Options& opt,
                     Result& res, std::string& err) {
    static std::mutex replace_mutex;
    std::unique_lock<std::mutex> replace_lock(replace_mutex,
                                              std::try_to_lock);
    if (!replace_lock.owns_lock()) {
        err = "another texture replacement is still applying; wait for it "
              "to finish, then retry";
        return false;
    }
    DebugLog::Scope debug_scope("Replace texture", img_path + " | " +
        target_bnk_path + " | index=" +
        std::to_string(target_file_index));
    res = Result{};

    progress_update(5, 100, "Loading " +
        std::filesystem::path(img_path).filename().string());
    ImageLoad::Image decoded;
    if (!ImageLoad::load_file(img_path, decoded, err)) {
        return false;
    }

    std::vector<PreparedReplacement> sets(1);
    if (!prepare_replacement(decoded, target_bnk_path, target_file_index,
                             opt, sets.front(), err)) {
        return false;
    }

    auto basename_of = [](const std::string& norm) {
        const size_t slash = norm.find_last_of('/');
        return slash == std::string::npos ? norm : norm.substr(slash + 1);
    };
    const std::string clicked_key =
        normalized_path(sets.front().groups.front().virtual_path);
    const std::string clicked_base = basename_of(clicked_key);
    std::set<std::string> seen_keys{clicked_key};
    std::vector<std::pair<std::string, int>> twin_seeds;
    {
        std::vector<std::string> paths = S.bnk_paths;
        paths.insert(paths.end(), S.nested_bnk_paths.begin(),
                     S.nested_bnk_paths.end());
        for (const std::string& path : paths) {
            if (texture_bank_role(path) != 1) continue;
            BnkCache::Entry entry;
            try {
                entry = BnkCache::get(path);
            } catch (...) {
                continue;
            }
            const auto& files = entry.reader->list_files();
            for (size_t i = 0; i < files.size(); ++i) {
                const std::string norm = normalized_path(files[i].name);
                if (basename_of(norm) != clicked_base ||
                    seen_keys.count(norm)) {
                    continue;
                }
                seen_keys.insert(norm);
                twin_seeds.emplace_back(path, (int)i);
                DebugLog::Write("texture-replace",
                    "same-name twin: " + files[i].name);
            }
        }
    }
    for (const auto& [seed_path, seed_index] : twin_seeds) {
        PreparedReplacement twin;
        if (!prepare_replacement(decoded, seed_path, seed_index, opt, twin,
                                 err)) {
            err = "same-name twin failed to prepare: " + err;
            return false;
        }
        sets.push_back(std::move(twin));
    }

    if (!apply_texture_replacement(sets, err)) {
        return false;
    }

    progress_update(95, 100, "Refreshing texture caches");
    std::string result_summary;
    for (const PreparedReplacement& set : sets) {
        const std::string& vpath = set.groups.front().virtual_path;
        res.tex_virtual_paths.push_back(vpath);
        res.notes.push_back(
            "replaced " + vpath + " (" +
            std::to_string(set.built.width) + "x" +
            std::to_string(set.built.height) + ", " +
            std::to_string(set.built.mip_count) + " mips) in " +
            std::to_string(set.groups.size()) + " bank scope(s) + " +
            std::to_string(set.shared_mip0.size()) +
            " shared mip0 entr(ies)");
        if (!result_summary.empty()) result_summary += ", ";
        result_summary += vpath;
    }
    debug_scope.Result("success | " + result_summary + " | identities=" +
                       std::to_string(sets.size()));
    return true;
}
