bool DecodeEhfTerrainAlbedoFromBytes(const std::vector<uint8_t>& ehf,
                                     uint32_t              cells_w,
                                     uint32_t              cells_h,
                                     std::vector<uint8_t>& out_rgba,
                                     int&                  out_w,
                                     int&                  out_h)
{
    out_rgba.clear();
    out_w = 0;
    out_h = 0;
    if (ehf.empty() || cells_w == 0 || cells_h == 0) return false;

    if (DecodeEhfEmbeddedTileComposite(ehf, cells_w, cells_h,
                                       out_rgba, out_w, out_h)) {
        return true;
    }

    const uint8_t* eh_d = ehf.data();
    const size_t   eh_n = ehf.size();
    size_t  best_off = SIZE_MAX;
    uint32_t best_W = 0, best_H = 0;
    uint32_t best_raw = 0;

    auto u32_at = [&](size_t off) -> uint32_t {
        return (uint32_t(eh_d[off  ]) << 24) | (uint32_t(eh_d[off+1]) << 16) |
               (uint32_t(eh_d[off+2]) <<  8) |  uint32_t(eh_d[off+3]);
    };

    for (size_t i = 0; i + 84 < eh_n; ++i) {
        if (eh_d[i] != 0xFF || eh_d[i+1] != 0xFF ||
            eh_d[i+2] != 0xFF || eh_d[i+3] != 0xFE) continue;

        const uint32_t W  = u32_at(i + 16);
        const uint32_t H  = u32_at(i + 20);
        const uint32_t PF = u32_at(i + 24);
        const uint32_t mip_off = u32_at(i + 32);
        if (W == 0 || H == 0 || W > 8192 || H > 8192) continue;
        if (PF != 35u) continue;
        if (mip_off != 0x54) continue;

        if (i + mip_off + 4 > eh_n) continue;
        const uint32_t raw_size = u32_at(i + mip_off);
        if (raw_size > best_raw) {
            best_raw  = raw_size;
            best_off  = i;
            best_W = W; best_H = H;
        }
    }

    if (best_off != SIZE_MAX) {
        auto u32 = [&](size_t off) -> uint32_t {
            return (uint32_t(eh_d[off  ]) << 24) | (uint32_t(eh_d[off+1]) << 16) |
                   (uint32_t(eh_d[off+2]) <<  8) |  uint32_t(eh_d[off+3]);
        };
        const uint32_t mip_table_offset = u32(best_off + 32);
        const size_t mip_at = best_off + mip_table_offset;
        if (mip_at + 8 < eh_n) {
            const uint32_t raw_size  = u32(mip_at);
            const uint32_t comp_size = u32(mip_at + 4);
            const size_t   zlib_at   = mip_at + 8;

            if (zlib_at + comp_size <= eh_n) {
                std::vector<uint8_t> body(raw_size);
                z_stream zs{};
                zs.next_in   = const_cast<Bytef*>(eh_d + zlib_at);
                zs.avail_in  = (uInt)comp_size;
                zs.next_out  = body.data();
                zs.avail_out = (uInt)raw_size;
                int rc_init = inflateInit2(&zs, 15);
                int rc      = (rc_init == Z_OK) ? inflate(&zs, Z_FINISH) : Z_ERRNO;
                const size_t produced = raw_size - zs.avail_out;
                inflateEnd(&zs);

                if (rc_init == Z_OK && produced == raw_size) {
                    std::vector<uint8_t> bc1;
                    int dec_w = 0, dec_h = 0;
                    std::string err;
                    if (lh_decode_compressed_mip(body.data(), body.size(),
                                                 dec_w, dec_h, bc1, &err,
                                                 false))
                    {
                        std::vector<uint8_t> rgba;
                        if (TextureAtlas::DecodeRawBc1ToRgba(
                                bc1.data(), bc1.size(),
                                dec_w, dec_h, rgba))
                        {
                            const uint32_t terrain_cells_w =
                                cells_w > 1 ? cells_w - 1 : cells_w;
                            const uint32_t terrain_cells_h =
                                cells_h > 1 ? cells_h - 1 : cells_h;
                            const size_t terrain_area =
                                size_t(terrain_cells_w) *
                                size_t(terrain_cells_h);
                            const size_t decoded_area =
                                size_t(dec_w) * size_t(dec_h);
                            if (decoded_area < terrain_area / 2) {
                                std::ostringstream os;
                                os << "ehf: embedded BC1 page @0x"
                                   << std::hex << best_off << std::dec
                                   << " decoded as " << dec_w << "x" << dec_h
                                   << ", too small for full terrain";
                                OutputLog::info(os.str());
                            } else {
                                out_rgba = std::move(rgba);
                                out_w    = dec_w;
                                out_h    = dec_h;
                                std::ostringstream os;
                                os << "ehf: huffman BC1 baked albedo @0x"
                                   << std::hex << best_off << std::dec
                                   << "  header=" << best_W << "x" << best_H
                                   << "  decoded=" << dec_w << "x" << dec_h;
                                OutputLog::success(os.str());
                                return true;
                            }
                        }
                    } else {
                        OutputLog::warn("ehf: lh_decode_compressed_mip failed: "
                                        + err);
                    }
                } else {
                    std::ostringstream os;
                    os << "ehf: zlib inflate failed rc=" << rc
                       << " produced=" << produced << " of " << raw_size;
                    OutputLog::warn(os.str());
                }
            }
        }
    }

    auto round_up_pow2 = [](uint32_t n) {
        uint32_t p = 1; while (p < n) p <<= 1; return p;
    };
    const uint32_t pow2_W = round_up_pow2(cells_w);
    const uint32_t pow2_H = round_up_pow2(cells_h);

    struct Cand { uint32_t W, H; size_t bytes; };
    std::vector<Cand> cands;
    auto add = [&](uint32_t w, uint32_t h) {
        if (w == 0 || h == 0) return;
        if ((w & 3u) != 0 || (h & 3u) != 0) return;
        cands.push_back({w, h, (size_t)w * h / 2});
    };
    add(pow2_W, pow2_H);
    add(cells_w & ~3u, cells_h & ~3u);
    add(1024, 1024);
    add(1024,  768);
    add( 768, 1024);
    add(1024,  512);
    add( 512, 1024);
    add( 768,  768);
    add( 512,  512);
    add( 256,  256);
    const float terrain_aspect = (float)cells_w / (float)cells_h;
    std::sort(cands.begin(), cands.end(),
              [](const Cand& a, const Cand& b){ return a.bytes > b.bytes; });
    cands.erase(std::unique(cands.begin(), cands.end(),
        [](const Cand& a, const Cand& b){
            return a.W == b.W && a.H == b.H; }), cands.end());
    auto aspect_ok = [&](uint32_t w, uint32_t h) -> bool {
        float a = (float)w / (float)h;
        return a > terrain_aspect * 0.25f && a < terrain_aspect * 4.0f;
    };

    const size_t n = ehf.size();
    const uint8_t* d = ehf.data();
    auto u32be = [](const uint8_t* p) -> uint32_t {
        return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
               (uint32_t(p[2]) <<  8) |  uint32_t(p[3]);
    };

    auto header_contradicts = [&](size_t zlib_at,
                                  uint32_t cand_w,
                                  uint32_t cand_h) -> bool {
        for (uint32_t mt = 0x54; mt <= 0x200; mt += 4) {
            if (zlib_at < size_t(mt) + 8) break;
            const size_t h0 = zlib_at - 8 - mt;
            if (u32be(d + h0) != 0xFFFFFFFEu) continue;
            if (u32be(d + h0 + 0x20) != mt) continue;
            const uint32_t pw = u32be(d + h0 + 0x10);
            const uint32_t ph = u32be(d + h0 + 0x14);
            if (pw == 0 || ph == 0 || pw > 8192 || ph > 8192) continue;
            return pw != cand_w || ph != cand_h;
        }
        return false;
    };

    struct Hit { uint32_t W, H; size_t bytes; size_t offset; uint32_t comp; };
    std::vector<Hit> hits;
    size_t i = 8;
    while (i + 2 < n) {
        if (d[i] == 0x78 &&
            (d[i+1] == 0xDA || d[i+1] == 0x9C ||
             d[i+1] == 0x01 || d[i+1] == 0x5E))
        {
            const uint32_t rs = u32be(d + i - 8);
            const uint32_t cs = u32be(d + i - 4);
            if (cs > 16 && (size_t)i + cs <= n) {
                for (const auto& c : cands) {
                    if (rs == (uint32_t)c.bytes &&
                        aspect_ok(c.W, c.H) &&
                        !header_contradicts(i, c.W, c.H))
                    {
                        hits.push_back({c.W, c.H, c.bytes, i, cs});
                        break;
                    }
                }
            }
        }
        ++i;
    }
    if (hits.empty()) {
        OutputLog::warn("ehf: no BC1 section matching any candidate (tried " +
                        std::to_string(cands.size()) + " sizes) found in " +
                        std::to_string(n) + "-byte .ehf");
        return false;
    }
    std::sort(hits.begin(), hits.end(),
              [](const Hit& a, const Hit& b){ return a.bytes > b.bytes; });
    const Hit& best = hits.front();

    const size_t per_cell_bytes = (size_t)(cells_w & ~3u) *
                                  (size_t)(cells_h & ~3u) / 2;
    {
        std::ostringstream os;
        os << "ehf: " << hits.size() << " BC1 candidate(s); picked "
           << best.W << "x" << best.H << " BC1 @0x" << std::hex
           << best.offset;
        OutputLog::info(os.str());
        if (best.bytes < per_cell_bytes / 2) {
            OutputLog::warn("ehf: picked page too small to be per-cell"
                            " baked albedo - falling back to atlas");
            return false;
        }
    }
    std::vector<uint8_t> rgba;
    if (!TextureAtlas::DecodeZlibBc1Page(d + best.offset, best.comp,
                                         best.bytes, (int)best.W, (int)best.H,
                                         rgba)) {
        OutputLog::warn("ehf: candidate at 0x" +
                        std::to_string((unsigned long long)best.offset) +
                        " (" + std::to_string(best.W) + "x" +
                        std::to_string(best.H) + " BC1) failed to decode");
        return false;
    }
    out_rgba = std::move(rgba);
    out_w    = (int)best.W;
    out_h    = (int)best.H;
    return true;
}

bool DecodeEhfTerrainAlbedo(const FlatAssetEntry& level_entry,
                            uint32_t              cells_w,
                            uint32_t              cells_h,
                            std::vector<uint8_t>& out_rgba,
                            int&                  out_w,
                            int&                  out_h)
{
    out_rgba.clear();
    out_w = 0;
    out_h = 0;

    if (!g_pending_terrain_ehf_bytes.empty() &&
        g_pending_terrain_level_entry.full_path == level_entry.full_path)
    {
        return DecodeEhfTerrainAlbedoFromBytes(
            g_pending_terrain_ehf_bytes,
            cells_w, cells_h, out_rgba, out_w, out_h);
    }

    for (const auto& fe : S.all_heightfield_files) {
        std::string nlow = fe.name;
        std::transform(nlow.begin(), nlow.end(), nlow.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        if (nlow.size() < 4 ||
            nlow.compare(nlow.size() - 4, 4, ".ehf") != 0) continue;
        try {
            auto v = BnkCache::extract_bytes(fe.bnk_path, fe.file_index);
            if (v.empty()) continue;
            std::vector<uint8_t> blob(v.begin(), v.end());
            if (DecodeEhfTerrainAlbedoFromBytes(blob, cells_w, cells_h,
                                                out_rgba, out_w, out_h))
                return true;
        } catch (...) {}
    }
    OutputLog::warn("ehf: no usable .ehf found for level "
                    + level_entry.name);
    return false;
}
