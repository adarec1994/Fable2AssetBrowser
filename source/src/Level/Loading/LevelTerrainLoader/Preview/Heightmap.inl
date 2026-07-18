bool RenderHeightmapToRGBA(const FlatAssetEntry& entry,
                           std::vector<uint8_t>& out_rgba,
                           int&                  out_w,
                           int&                  out_h)
{
    out_rgba.clear();
    out_w = 0;
    out_h = 0;

    std::filesystem::path lp = entry.full_path;
    lp.replace_extension(".list");
    std::string list_full = lp.string();
    std::string list_key  = list_full;
    std::transform(list_key.begin(), list_key.end(), list_key.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    std::replace(list_key.begin(), list_key.end(), '\\', '/');

    int list_idx = BnkCache::find_index(entry.bnk_path, list_key);
    if (list_idx < 0) {
        OutputLog::error("View Heightmap: no companion .list ("
                         + list_full + ") in BNK");
        return false;
    }

    std::vector<uint8_t> list_bytes;
    try {
        list_bytes = BnkCache::extract_bytes(entry.bnk_path, list_idx);
    } catch (...) {
        OutputLog::error("View Heightmap: failed to extract .list");
        return false;
    }
    std::string list_str(reinterpret_cast<const char*>(list_bytes.data()),
                         list_bytes.size());

    std::string ghf_path;
    size_t pos = 0;
    while (pos < list_str.size()) {
        size_t eol = list_str.find_first_of("\r\n", pos);
        std::string line = (eol == std::string::npos)
                               ? list_str.substr(pos)
                               : list_str.substr(pos, eol - pos);
        pos = (eol == std::string::npos)
                  ? list_str.size()
                  : list_str.find_first_not_of("\r\n", eol);
        if (pos == std::string::npos) pos = list_str.size();
        if (line.empty()) continue;

        std::string low = line;
        std::transform(low.begin(), low.end(), low.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        if (low.size() >= 4 && low.compare(low.size()-4, 4, ".ghf") == 0) {
            ghf_path = line;
            break;
        }
    }
    if (ghf_path.empty()) {
        OutputLog::error("View Heightmap: no .ghf entry in .list");
        return false;
    }

    HeightfieldFiles hf;
    if (!LoadHeightfieldFiles({}, ghf_path, {}, {}, hf)) {
        OutputLog::error("View Heightmap: .ghf load failed: " + hf.error);
        return false;
    }

    GhfHeights hg;
    if (!DecodeGhfHeights(hf.ghf_bytes_raw, hg)) {
        OutputLog::error("View Heightmap: .ghf decode failed: " + hg.error);
        return false;
    }

    const float lo   = hg.min_height;
    const float hi   = hg.max_height;
    const float span = (hi > lo) ? (hi - lo) : 1.f;

    out_w = static_cast<int>(hg.width);
    out_h = static_cast<int>(hg.height);
    out_rgba.resize(static_cast<size_t>(out_w) * static_cast<size_t>(out_h) * 4);

    for (size_t i = 0; i < hg.heights.size(); ++i) {
        float t = (hg.heights[i] - lo) / span;
        if (t < 0.f) t = 0.f;
        if (t > 1.f) t = 1.f;
        const uint8_t v = static_cast<uint8_t>(t * 255.0f + 0.5f);
        out_rgba[i * 4 + 0] = v;
        out_rgba[i * 4 + 1] = v;
        out_rgba[i * 4 + 2] = v;
        out_rgba[i * 4 + 3] = 0xFF;
    }

    return true;
}
