static bool parse_prop_model_buffer(const std::vector<unsigned char>& buf,
                                    const std::string& model_path,
                                    CachedPropModel& out,
                                    std::string* reason = nullptr)
{
    CachedPropModel tmp;
    const bool main_ok = parse_mdl_info(buf, tmp.info, model_path);

    auto missing_count = [&]() -> size_t {
        size_t empty = 0;
        if (tmp.info.MeshBuffers.size() < tmp.info.MeshCount) {
            empty += tmp.info.MeshCount - tmp.info.MeshBuffers.size();
        }
        for (const auto& mb : tmp.info.MeshBuffers) {
            if (mb.VertexCount == 0) ++empty;
        }
        return empty;
    };

    if (main_ok) {
        bool all_empty = !tmp.info.MeshBuffers.empty();
        for (const auto& mb : tmp.info.MeshBuffers) {
            if (mb.VertexCount > 0) {
                all_empty = false;
                break;
            }
        }
        if (all_empty) {
            reparse_mdl_buffers_via_polymsh_scan(buf, tmp.info);
        }
    }

    if (missing_count() > 0) {
        reparse_mdl_missing_buffers_optstr(buf, tmp.info);
    }
    if (missing_count() > 0) {
        reparse_mdl_as_foliage_48b(buf, tmp.info);
    }
    {
        std::string lp = model_path;
        std::transform(lp.begin(), lp.end(), lp.begin(),
                       [](unsigned char c){ return (char)std::tolower(c); });
        std::replace(lp.begin(), lp.end(), '\\', '/');
        const bool multi_instance_target =
            lp.find("bs_townhouse_basic_snow_v2") != std::string::npos &&
            (lp.find("/exterior.mdl") != std::string::npos ||
             lp.find("/interior.mdl") != std::string::npos);
        if (multi_instance_target) {
            reparse_mdl_multi_instance_buffers(buf, tmp.info);
        }
    }

    if (!main_ok && missing_count() >= tmp.info.MeshCount) {
        if (reason) {
            *reason = "parse_mdl_info failed, bytes=" +
                      std::to_string(buf.size());
        }
        return false;
    }

    const bool has_mesh_header =
        buf.size() >= 8 &&
        (std::memcmp(buf.data(), "MeshFile", 8) == 0 ||
         std::memcmp(buf.data(), "DefMeshF", 8) == 0);
    if (has_mesh_header) {
        tmp.geoms.clear();
        if (!build_mdl_engine_geometry(buf, tmp.geoms) || tmp.geoms.empty()) {
            if (reason) {
                *reason = "engine decode produced 0 geoms, bytes=" +
                          std::to_string(buf.size());
            }
            return false;
        }
    } else {
        parse_mdl_geometry(buf, tmp.info, tmp.geoms);
        if (tmp.geoms.empty()) {
            if (reason) {
                *reason = "parse_mdl_geometry produced 0 geoms"
                          ", bytes=" + std::to_string(buf.size()) +
                          ", meshes=" + std::to_string(tmp.info.Meshes.size()) +
                          ", buffers=" +
                          std::to_string(tmp.info.MeshBuffers.size());
            }
            return false;
        }
    }

    out.info = std::move(tmp.info);
    out.geoms = std::move(tmp.geoms);
    if (reason) {
        *reason = "ok, geoms=" + std::to_string(out.geoms.size());
    }
    return true;
}

static std::vector<const FlatAssetEntry*>
collect_prop_model_candidates(const std::string& model_path,
                              const std::string& preferred_body_bnk)
{
    const std::string want_full = normalized_asset_path(model_path);
    const std::string want_leaf = normalized_asset_path(asset_leaf(model_path));
    const std::string preferred_bnk = normalized_asset_path(preferred_body_bnk);
    const std::string preferred_parent = asset_parent_key(preferred_body_bnk);

    struct Scored {
        const FlatAssetEntry* entry = nullptr;
        int score = 0;
    };

    std::vector<Scored> exact;
    std::vector<Scored> leaf;
    exact.reserve(16);
    leaf.reserve(16);

    for (const auto& e : S.all_mdl_files) {
        const std::string e_full = normalized_asset_path(e.full_path);
        const std::string e_leaf = normalized_asset_path(e.name);
        const bool full_match = (e_full == want_full);
        const bool leaf_match = (!full_match && !want_leaf.empty() &&
                                 e_leaf == want_leaf);
        if (!full_match && !leaf_match) continue;

        int score = full_match ? 100000 : 10000;
        const std::string e_bnk = normalized_asset_path(e.bnk_path);
        if (!preferred_bnk.empty() && e_bnk == preferred_bnk) {
            score += 50000;
        }
        if (!preferred_parent.empty() &&
            asset_parent_key(e.bnk_path) == preferred_parent) {
            score += 10000;
        }
        if (!e.from_nested) score += 1000;
        score += std::min<uint32_t>(e.size, 1000000u) / 10000;

        (full_match ? exact : leaf).push_back({&e, score});
    }

    auto by_score = [](const Scored& a, const Scored& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.entry->bnk_path < b.entry->bnk_path;
    };

    auto& picked = exact.empty() ? leaf : exact;
    std::sort(picked.begin(), picked.end(), by_score);

    std::vector<const FlatAssetEntry*> out;
    out.reserve(picked.size());
    for (const auto& s : picked) out.push_back(s.entry);
    return out;
}

static bool try_prop_model_candidate(const FlatAssetEntry& entry,
                                     const std::string& model_path,
                                     CachedPropModel& cached,
                                     std::string& method,
                                     std::string* fail_reason = nullptr)
{
    std::vector<unsigned char> buf;
    if (build_mdl_buffer_for_name_with_body(model_path, entry.bnk_path, buf)) {
        std::string parse_reason;
        if (parse_prop_model_buffer(buf, model_path, cached, &parse_reason)) {
            method = "body+header";
            return true;
        }
        if (fail_reason) {
            *fail_reason = "body+header parse rejected: " + parse_reason;
        }
    } else if (fail_reason) {
        *fail_reason = "body+header build failed";
    }

    try {
        std::vector<unsigned char> body =
            BnkCache::extract_bytes(entry.bnk_path, entry.file_index);
        if (!body.empty()) {
            std::string parse_reason;
            if (parse_prop_model_buffer(body, model_path, cached,
                                        &parse_reason)) {
                method = "body";
                return true;
            }
            if (fail_reason) {
                if (!fail_reason->empty()) *fail_reason += "; ";
                *fail_reason += "body parse rejected: " + parse_reason;
            }
        } else if (fail_reason) {
            if (!fail_reason->empty()) *fail_reason += "; ";
            *fail_reason += "body extract returned 0 bytes";
        }
    } catch (...) {
        if (fail_reason) {
            if (!fail_reason->empty()) *fail_reason += "; ";
            *fail_reason += "body extract threw";
        }
    }

    return false;
}

static bool load_cached_prop_model(const std::string& model_path,
                                   const std::string& preferred_body_bnk,
                                   CachedPropModel& cached)
{
    const bool shell_pair_model = is_shell_pair_model_path(model_path);
    const std::string want_full = normalized_asset_path(model_path);

    if (shell_pair_model) {
        std::vector<const FlatAssetEntry*> candidates =
            collect_prop_model_candidates(model_path, preferred_body_bnk);
        for (const FlatAssetEntry* candidate : candidates) {
            if (!candidate ||
                normalized_asset_path(candidate->full_path) != want_full) {
                continue;
            }
            const FlatAssetEntry& entry = *candidate;

            std::string method;
            std::string fail_reason;
            if (try_prop_model_candidate(entry, model_path, cached, method,
                                         &fail_reason)) {
                return true;
            }
        }
    }

    std::vector<unsigned char> buf;
    if (build_mdl_buffer_for_name_with_body(model_path,
                                            preferred_body_bnk,
                                            buf)) {
        std::string parse_reason;
        if (parse_prop_model_buffer(buf, model_path, cached, &parse_reason)) {
            return true;
        }
    }

    const auto candidates =
        collect_prop_model_candidates(model_path, preferred_body_bnk);
    for (const FlatAssetEntry* entry : candidates) {
        if (!entry) continue;
        std::string method;
        std::string fail_reason;
        if (try_prop_model_candidate(*entry, model_path, cached, method,
                                     &fail_reason)) {
            return true;
        }
    }

    return false;
}
