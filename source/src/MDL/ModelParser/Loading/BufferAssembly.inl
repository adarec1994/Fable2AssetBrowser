static void remap_known_mdl_texture_path(std::string& tex_name) {
    std::string key = tex_name;
    std::transform(key.begin(), key.end(), key.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    std::replace(key.begin(), key.end(), '\\', '/');

    static const char* const kBadWagonColourPath =
        "art/environment/regions/bowerstone/buildings/pictures/"
        "bs_market_tarotstall/wagon_colour_01.tex";
    if (key == kBadWagonColourPath) {
        tex_name =
            "art\\environment\\shared assets\\props\\pictures\\"
            "_sharedtextures\\wagon_colour_01.tex";
    }
}

bool build_mdl_buffer_for_name_with_body(const std::string& mdl_name,
                                         const std::string& preferred_body_bnk,
                                         std::vector<unsigned char>& out)
{
    out.clear();

    std::string full_key = mdl_name;
    std::transform(full_key.begin(), full_key.end(),
                   full_key.begin(), ::tolower);
    std::replace(full_key.begin(), full_key.end(), '\\', '/');

    std::string base_key = full_key;
    {
        size_t sl = base_key.find_last_of('/');
        if (sl != std::string::npos) base_key = base_key.substr(sl + 1);
    }
    const bool allow_base_fallback =
        base_key != full_key &&
        base_key != "exterior.mdl" &&
        base_key != "interior.mdl";

    auto find_with_fallback = [&](const std::string& bnk) -> int {
        int idx = BnkCache::find_index(bnk, full_key);
        if (idx >= 0) return idx;
        if (allow_base_fallback) {
            idx = BnkCache::find_index(bnk, base_key);
        }
        return idx;
    };

    std::vector<std::string> header_candidates;
    std::vector<std::string> body_candidates;

    auto add_unique = [](std::vector<std::string>& dst,
                         const std::optional<std::string>& v)
    {
        if (!v || v->empty()) return;
        if (std::find(dst.begin(), dst.end(), *v) == dst.end()) {
            dst.push_back(*v);
        }
    };

    if (!preferred_body_bnk.empty()) {
        body_candidates.push_back(preferred_body_bnk);
        size_t slash = preferred_body_bnk.find_last_of("/\\");
        std::string body_leaf = (slash == std::string::npos)
            ? preferred_body_bnk
            : preferred_body_bnk.substr(slash + 1);
        std::transform(body_leaf.begin(), body_leaf.end(),
                       body_leaf.begin(), ::tolower);
        const std::string suffix = "_models.bnk";
        if (body_leaf.size() >= suffix.size() &&
            body_leaf.compare(body_leaf.size() - suffix.size(),
                              suffix.size(), suffix) == 0) {
            std::string paired =
                body_leaf.substr(0, body_leaf.size() - suffix.size())
                + "_model_headers.bnk";
            add_unique(header_candidates, find_bnk_by_filename(paired));
        }
    }

    add_unique(header_candidates, find_bnk_by_filename("globals_model_headers.bnk"));
    add_unique(body_candidates, find_bnk_by_filename("globals_models.bnk"));

    for (const auto& header_bnk : header_candidates) {
        const int hidx = find_with_fallback(header_bnk);
        if (hidx < 0) continue;
        for (const auto& body_bnk : body_candidates) {
            const int ridx = find_with_fallback(body_bnk);
            if (ridx < 0) continue;
            try {
                auto vh = BnkCache::extract_bytes(header_bnk, hidx);
                auto vr = BnkCache::extract_bytes(body_bnk, ridx);
                out.reserve(vh.size() + vr.size());
                out.insert(out.end(), vh.begin(), vh.end());
                out.insert(out.end(), vr.begin(), vr.end());
                return !out.empty();
            } catch (...) {
                out.clear();
            }
        }
    }

    for (const auto& body_bnk : body_candidates) {
        const int ridx = find_with_fallback(body_bnk);
        if (ridx < 0) continue;
        try {
            out = BnkCache::extract_bytes(body_bnk, ridx);
            if (!out.empty()) return true;
        } catch (...) {
            out.clear();
        }
    }

    {
        const FlatAssetEntry* hit = nullptr;
        for (const auto& e : S.all_mdl_files) {
            std::string np = e.full_path;
            std::transform(np.begin(), np.end(), np.begin(), ::tolower);
            std::replace(np.begin(), np.end(), '\\', '/');
            if (np == full_key) { hit = &e; break; }
        }
        if (!hit && allow_base_fallback) {
            for (const auto& e : S.all_mdl_files) {
                std::string nm = e.name;
                std::transform(nm.begin(), nm.end(), nm.begin(), ::tolower);
                if (nm == base_key) { hit = &e; break; }
            }
        }

        if (hit) {
            try {
                auto vr = BnkCache::extract_bytes(hit->bnk_path, hit->file_index);
                if (!vr.empty()) {
                    std::vector<std::string> dyn_header_candidates;
                    {
                        std::string body_path = hit->bnk_path;
                        std::string body_leaf =
                            std::filesystem::path(body_path).filename().string();
                        std::string lower = body_leaf;
                        std::transform(lower.begin(), lower.end(),
                                       lower.begin(), ::tolower);
                        const std::string suf = "_models.bnk";
                        if (lower.size() > suf.size() &&
                            lower.compare(lower.size() - suf.size(),
                                          suf.size(), suf) == 0)
                        {
                            std::string sibling_leaf =
                                body_leaf.substr(0, body_leaf.size() - suf.size())
                                + "_model_headers.bnk";
                            std::filesystem::path sib_path(body_path);
                            sib_path.replace_filename(sibling_leaf);
                            dyn_header_candidates.push_back(sib_path.string());
                        }
                    }
                    {
                        auto it = S.nested_bnk_parents.find(hit->bnk_path);
                        if (it != S.nested_bnk_parents.end()) {
                            const std::string& parent = it->second;
                            for (const auto& sib : S.nested_bnk_paths) {
                                auto sib_it = S.nested_bnk_parents.find(sib);
                                if (sib_it == S.nested_bnk_parents.end()) continue;
                                if (sib_it->second != parent) continue;
                                std::string sib_leaf =
                                    std::filesystem::path(sib).filename().string();
                                std::transform(sib_leaf.begin(), sib_leaf.end(),
                                               sib_leaf.begin(), ::tolower);
                                if (sib_leaf.find("header") != std::string::npos &&
                                    sib_leaf.find("model")  != std::string::npos)
                                {
                                    add_unique(dyn_header_candidates, sib);
                                }
                            }
                        }
                    }

                    std::vector<uint8_t> vh;
                    for (const auto& hdr : dyn_header_candidates) {
                        const int hidx = find_with_fallback(hdr);
                        if (hidx < 0) continue;
                        try {
                            vh = BnkCache::extract_bytes(hdr, hidx);
                            if (!vh.empty()) break;
                        } catch (...) { vh.clear(); }
                    }
                    if (vh.empty()) {
                        for (const auto& header_bnk : header_candidates) {
                            const int hidx = find_with_fallback(header_bnk);
                            if (hidx < 0) continue;
                            try {
                                vh = BnkCache::extract_bytes(header_bnk, hidx);
                                if (!vh.empty()) break;
                            } catch (...) { vh.clear(); }
                        }
                    }
                    out.reserve(vh.size() + vr.size());
                    out.insert(out.end(), vh.begin(), vh.end());
                    out.insert(out.end(), vr.begin(), vr.end());
                    return !out.empty();
                }
            } catch (...) {
                out.clear();
            }
        }
    }

    return false;
}

bool build_mdl_buffer_for_name(const std::string &mdl_name, std::vector<unsigned char> &out){
    return build_mdl_buffer_for_name_with_body(mdl_name, std::string(), out);
}

