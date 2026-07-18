bool DecodeEhfPaletteFirstDiffuse(const std::vector<uint8_t>& ehf,
                                  std::vector<uint8_t>& out_rgba,
                                  int&                  out_w,
                                  int&                  out_h,
                                  float&                out_tile_scale,
                                  std::string&          out_picked_name)
{
    out_rgba.clear();
    out_w = 0;
    out_h = 0;
    out_tile_scale = 1.0f;
    out_picked_name.clear();
    if (ehf.empty()) return false;

    EhfPalette::Palette pal = EhfPalette::Parse(ehf);
    if (!pal.ok || pal.entries.empty()) {
        OutputLog::warn("ehf palette: parse failed or empty");
        return false;
    }

    auto basename_lower = [](const std::string& path) {
        std::string base = std::filesystem::path(path).filename().string();
        std::transform(base.begin(), base.end(), base.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        return base;
    };

    OutputLog::info("ehf palette: searching " +
                    std::to_string(S.all_tex_files.size()) +
                    " indexed .tex files for " +
                    std::to_string(pal.entries.size()) +
                    " palette diffuse references...");

    for (size_t pi = 0; pi < pal.entries.size(); ++pi) {
        const auto& e = pal.entries[pi];
        const std::string want = basename_lower(e.diffuse_path);
        if (want.empty()) continue;

        const FlatAssetEntry* hit = nullptr;
        for (const auto& tex : S.all_tex_files) {
            std::string nm = std::filesystem::path(tex.name)
                                 .filename().string();
            std::transform(nm.begin(), nm.end(), nm.begin(),
                           [](unsigned char c){ return std::tolower(c); });
            if (nm == want) { hit = &tex; break; }
        }
        if (!hit) {
            if (pi < 6) {
                OutputLog::info("  [" + std::to_string(pi) +
                                "] not found: " + want);
            }
            continue;
        }

        std::vector<uint8_t> blob;
        try {
            auto v = BnkCache::extract_bytes(hit->bnk_path, hit->file_index);
            if (!v.empty()) blob.assign(v.begin(), v.end());
        } catch (...) {}
        if (blob.empty()) {
            OutputLog::warn("  [" + std::to_string(pi) +
                            "] " + want + " found in " + hit->bnk_path +
                            " but extract returned empty");
            continue;
        }

        std::vector<unsigned char> blob_uc(blob.begin(), blob.end());
        std::vector<uint8_t> rgba;
        bool has_alpha = false;
        int w = 0, h = 0;
        if (!decode_tex_to_rgba(blob_uc, rgba, w, h, &has_alpha, -1)) {
            const std::string& reason = mp_last_decode_fail_reason();
            const std::string& info   = mp_last_decode_info();
            OutputLog::warn("  [" + std::to_string(pi) + "] " + want +
                            " decode failed: " + reason +
                            (info.empty() ? "" : " (" + info + ")"));
            continue;
        }

        out_rgba = std::move(rgba);
        out_w = w;
        out_h = h;
        out_tile_scale = e.tile_scale;
        out_picked_name = basename_lower(e.diffuse_path);
        std::ostringstream os;
        os << "ehf palette: picked entry " << pi << " '" << out_picked_name
           << "' (" << w << "x" << h
           << ", tile_scale=" << e.tile_scale << ")";
        OutputLog::success(os.str());
        return true;
    }

    OutputLog::warn("ehf palette: NONE of " +
                    std::to_string(pal.entries.size()) +
                    " palette diffuse .tex files found in the "
                    + std::to_string(S.all_tex_files.size())
                    + "-entry global .tex index");
    return false;
}
