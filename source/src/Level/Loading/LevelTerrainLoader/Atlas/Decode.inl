bool DecodeLevelTextureAtlas(const FlatAssetEntry& level_entry,
                             std::vector<uint8_t>& out_rgba,
                             int&                  out_w,
                             int&                  out_h)
{
    out_rgba.clear();
    out_w = 0;
    out_h = 0;

    std::filesystem::path atlas_path = level_entry.full_path;
    atlas_path.replace_extension(".texture_atlas");
    const std::string atlas_full = atlas_path.string();

    std::string atlas_key = atlas_full;
    std::transform(atlas_key.begin(), atlas_key.end(), atlas_key.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    std::replace(atlas_key.begin(), atlas_key.end(), '\\', '/');

    auto try_bnk = [&](const std::string& bnk_path,
                       std::vector<uint8_t>& out_blob) -> bool {
        int idx = BnkCache::find_index(bnk_path, atlas_key);
        if (idx < 0) return false;
        try {
            auto v = BnkCache::extract_bytes(bnk_path, idx);
            if (v.empty()) return false;
            out_blob.assign(v.begin(), v.end());
            return true;
        } catch (...) {
            return false;
        }
    };

    std::vector<uint8_t> blob;
    bool found = try_bnk(level_entry.bnk_path, blob);

    if (!found) {
        const std::string base_lower = std::filesystem::path(atlas_full)
                                           .filename().string();
        std::string base_low = base_lower;
        std::transform(base_low.begin(), base_low.end(), base_low.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        for (const auto& fe : S.all_heightfield_files) {
            std::string nlow = fe.name;
            std::transform(nlow.begin(), nlow.end(), nlow.begin(),
                           [](unsigned char c){ return std::tolower(c); });
            if (nlow != base_low) continue;
            try {
                auto v = BnkCache::extract_bytes(fe.bnk_path, fe.file_index);
                if (!v.empty()) {
                    blob.assign(v.begin(), v.end());
                    found = true;
                    break;
                }
            } catch (...) {}
        }
    }

    if (!found) {
        for (const auto& bnk_path : S.bnk_paths) {
            if (bnk_path == level_entry.bnk_path) continue;
            if (try_bnk(bnk_path, blob)) { found = true; break; }
        }
    }
    if (!found) {
        OutputLog::warn("texture_atlas: no '" + atlas_full +
                        "' found in any loaded BNK");
        return false;
    }

    TextureAtlas::DecodedAtlas dec = TextureAtlas::DecodeAtlas(blob);
    if (!dec.ok) {
        OutputLog::error("texture_atlas: " + dec.error +
                         "  (file=" + atlas_full + ")");
        return false;
    }
    out_rgba = std::move(dec.rgba);
    out_w    = dec.width;
    out_h    = dec.height;
    return true;
}
