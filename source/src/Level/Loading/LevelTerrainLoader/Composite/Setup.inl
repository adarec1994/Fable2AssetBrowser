bool BakeEhfTerrainCompositeWithBnk(const std::vector<uint8_t>& ehf,
                                    const std::string& preferred_bnk,
                                    std::vector<uint8_t>&  out_rgba,
                                    int&                   out_w,
                                    int&                   out_h,
                                    std::string&           out_picked_name,
                                    bool                   allow_embedded_albedo)
{
    out_rgba.clear();
    out_w = 0;
    out_h = 0;
    out_picked_name.clear();
    if (ehf.empty()) return false;

    HeightfieldHeader hdr;
    {
        static constexpr char   kMagic[]   = "HeightFieldGraphicsFile";
        static constexpr size_t kMagicLen  = sizeof(kMagic) - 1;
        static constexpr size_t kHeaderLen = 63;
        if (ehf.size() < kHeaderLen) return false;
        if (std::memcmp(ehf.data(), kMagic, kMagicLen) != 0) return false;
        auto be_u32 = [&](size_t off) -> uint32_t {
            return (uint32_t(ehf[off]) << 24) | (uint32_t(ehf[off+1]) << 16)
                 | (uint32_t(ehf[off+2]) << 8) |  uint32_t(ehf[off+3]);
        };
        hdr.magic.assign(kMagic);
        hdr.version     = be_u32(kMagicLen);
        hdr.u0          = be_u32(35);
        hdr.u1          = be_u32(39);
        hdr.body_offset = be_u32(55);
        hdr.body_size   = be_u32(59);
        hdr.ok          = (uint64_t(hdr.body_offset) + hdr.body_size <= ehf.size());
    }
    if (!hdr.ok || hdr.u0 == 0 || hdr.u1 == 0) {
        OutputLog::warn("bake composite: bad .ehf header");
        return false;
    }

    EhfParsedBody parsed;
    const bool parsed_ok = ParseEhfBody(ehf, parsed);
    if (parsed_ok) {
        std::ostringstream pos;
        pos << "ehf chunk parse: " << parsed.chunk_w << "x"
            << parsed.chunk_h << " chunks, "
            << parsed.lods.size() << " LODs"
            << "  (consumed " << parsed.bytes_consumed
            << "B, remaining " << parsed.bytes_remaining << "B)";
        OutputLog::success(pos.str());

        std::vector<TerrainTextureRegistry::LodPaletteEntry> pe;
        pe.reserve(parsed.lods.size());
        for (const auto& L : parsed.lods) {
            TerrainTextureRegistry::LodPaletteEntry e;
            e.base_diffuse   = L.strs[0];
            e.base_normal    = L.strs[1];
            e.detail_diffuse = L.strs[3];
            e.detail_normal  = L.strs[4];
            e.base_tile_scale   = L.params[0][0];
            e.base_intensity    = L.params[0][1];
            e.detail_tile_scale = L.params[1][0];
            e.detail_intensity  = L.params[1][1];
            pe.push_back(std::move(e));
        }
        TerrainTextureRegistry::SetLodPalette(std::move(pe));
    } else {
        OutputLog::warn("bake composite: chunk parse failed: " + parsed.error);
    }

    if (allow_embedded_albedo &&
        DecodeEhfTerrainAlbedoFromBytes(ehf, hdr.u0, hdr.u1,
                                        out_rgba, out_w, out_h))
    {
        out_picked_name = "embedded_tile_albedo";
        return true;
    }

    if (!parsed_ok) return false;

    std::vector<uint8_t> lm_rgba;
    int lm_w = 0, lm_h = 0;
    {
        const uint8_t* p = ehf.data() + hdr.body_offset;
        std::vector<uint8_t> body_slice(p, p + hdr.body_size);
        auto dec = TextureAtlas::DecodeAtlas(body_slice);
        if (!dec.ok || dec.pixel_format != 24u) {
            OutputLog::warn("bake composite: .ehf body decode failed: " +
                            dec.error);
            return false;
        }
        lm_rgba = std::move(dec.rgba);
        lm_w    = dec.width;
        lm_h    = dec.height;
    }

    auto basename_lower = [](const std::string& path) {
        std::string base = std::filesystem::path(path).filename().string();
        std::transform(base.begin(), base.end(), base.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        return base;
    };

    struct Mat {
        bool                 decoded = false;
        std::vector<uint8_t> rgba;
        int                  w = 0, h = 0;
        std::string          name;
        float                tile_scale = 0.125f;
    };
    EhfPalette::Palette pal = EhfPalette::Parse(ehf);
    std::vector<Mat> mats(parsed.lods.size());
    int first_decoded = -1;
    for (size_t li = 0; li < parsed.lods.size(); ++li) {
        const std::string diffuse_path = parsed.lods[li].strs[0];
        if (diffuse_path.empty()) continue;
        const std::string want = basename_lower(diffuse_path);

        std::vector<unsigned char> blob_uc;
        bool stitched = false;
        try {
            stitched = build_any_tex_buffer_for_name(want, blob_uc,
                                                    preferred_bnk);
        } catch (...) { stitched = false; }
        if (!stitched || blob_uc.empty()) {
            const FlatAssetEntry* hit = nullptr;
            for (const auto& tex : S.all_tex_files) {
                std::string nm = std::filesystem::path(tex.name)
                                     .filename().string();
                std::transform(nm.begin(), nm.end(), nm.begin(),
                               [](unsigned char c){ return std::tolower(c); });
                if (nm == want) { hit = &tex; break; }
            }
            if (!hit) continue;
            try {
                auto v = BnkCache::extract_bytes(hit->bnk_path,
                                                 hit->file_index);
                if (!v.empty()) blob_uc.assign(v.begin(), v.end());
            } catch (...) {}
            if (blob_uc.empty()) continue;
        }

        std::vector<uint8_t> rgba;
        bool has_alpha = false;
        int w = 0, h = 0;
        if (!decode_tex_to_rgba(blob_uc, rgba, w, h, &has_alpha, -1)) continue;
        mats[li].decoded = true;
        mats[li].rgba    = std::move(rgba);
        mats[li].w       = w;
        mats[li].h       = h;
        mats[li].name    = want;
        for (const auto& pe : pal.entries) {
            std::string pn = std::filesystem::path(pe.diffuse_path)
                                 .filename().string();
            std::transform(pn.begin(), pn.end(), pn.begin(),
                           [](unsigned char c){ return std::tolower(c); });
            if (pn == want) {
                mats[li].tile_scale = pe.tile_scale;
                break;
            }
        }
        if (first_decoded < 0) first_decoded = (int)li;
    }

    if (first_decoded < 0) {
        OutputLog::warn("bake composite: no LOD diffuse texture decoded");
        return false;
    }

    {
        int n = 0;
        for (auto& m : mats) if (m.decoded) ++n;
        std::ostringstream os;
        os << "decoded " << n << " of " << mats.size() << " LOD diffuses:";
        OutputLog::info(os.str());
        for (size_t i = 0; i < mats.size() && i < 8; ++i) {
            if (!mats[i].decoded) continue;
            OutputLog::info("  LOD[" + std::to_string(i) + "] "
                            + mats[i].name);
        }
    }
    out_picked_name = "chunkgrid["
        + std::to_string(parsed.chunk_w) + "x"
        + std::to_string(parsed.chunk_h) + " x "
        + std::to_string(mats.size()) + " LODs]";

    if (g_capture_splat_output && g_splat_output_rgba) {
        g_splat_output_rgba->clear();
        if (g_splat_output_w) *g_splat_output_w = 0;
        if (g_splat_output_h) *g_splat_output_h = 0;
    }

    const size_t pix = size_t(lm_w) * size_t(lm_h);
    out_rgba.assign(pix * 4, 0);
    out_w = lm_w;
    out_h = lm_h;

    float world_min_x =  1e30f;
    float world_min_z =  1e30f;
    float world_max_x = -1e30f;
    float world_max_z = -1e30f;
    for (const auto& c : parsed.chunks) {
        world_min_x = std::min(world_min_x, c.origin[0]);
        world_min_z = std::min(world_min_z, c.origin[1]);
        world_max_x = std::max(world_max_x, c.extent[0]);
        world_max_z = std::max(world_max_z, c.extent[1]);
    }
    const float world_span_x = std::max(1e-6f, world_max_x - world_min_x);
    const float world_span_z = std::max(1e-6f, world_max_z - world_min_z);
    const float chunk_size_x = world_span_x / std::max(1u, parsed.chunk_w);
    const float chunk_size_z = world_span_z / std::max(1u, parsed.chunk_h);
    std::vector<const EhfChunk*> chunk_grid(
        size_t(parsed.chunk_w) * size_t(parsed.chunk_h), nullptr);
    for (const auto& c : parsed.chunks) {
        const int cx = std::clamp(
            int(std::lround((c.origin[0] - world_min_x) / chunk_size_x)),
            0, int(parsed.chunk_w) - 1);
        const int cy = std::clamp(
            int(std::lround((c.origin[1] - world_min_z) / chunk_size_z)),
            0, int(parsed.chunk_h) - 1);
        chunk_grid[size_t(cy) * size_t(parsed.chunk_w) + size_t(cx)] = &c;
    }
    {
        std::ostringstream os;
        os << "ehf chunk world bounds: x=[" << world_min_x << ".."
           << world_max_x << "] z=[" << world_min_z << ".."
           << world_max_z << "] chunk=(" << chunk_size_x << ","
           << chunk_size_z << ")";
        OutputLog::info(os.str());
    }
