bool BuildEhfVistaPatchGeoms(
    const std::vector<uint8_t>& ehf,
    std::vector<VistaPatchGeom>& out_geoms,
    std::string* out_stats)
{
    return build_ehf_vista_patch_geoms(ehf, out_geoms, out_stats);
}

bool BakeEhfVistaPageComposite(const std::vector<uint8_t>& ehf,
                                      std::vector<uint8_t>& out_rgba,
                                      int&                  out_w,
                                      int&                  out_h,
                                      std::string&          out_name)
{
    out_rgba.clear();
    out_w = 0;
    out_h = 0;
    out_name.clear();

    EhfParsedBody parsed;
    if (!ParseEhfBody(ehf, parsed)) {
        OutputLog::warn("vista pages: body parse failed: " + parsed.error);
        return false;
    }
    if (parsed.bg_patches.empty()) {
        OutputLog::warn("vista pages: no bg patches in body");
        return false;
    }

    float min_x =  std::numeric_limits<float>::infinity();
    float min_z =  std::numeric_limits<float>::infinity();
    float max_x = -std::numeric_limits<float>::infinity();
    float max_z = -std::numeric_limits<float>::infinity();
    for (const EhfBgPatch& p : parsed.bg_patches) {
        if (!std::isfinite(p.aabb_min[0]) || !std::isfinite(p.aabb_min[1]) ||
            !std::isfinite(p.aabb_max[0]) || !std::isfinite(p.aabb_max[1])) {
            continue;
        }
        min_x = std::min(min_x, p.aabb_min[0]);
        min_z = std::min(min_z, p.aabb_min[1]);
        max_x = std::max(max_x, p.aabb_max[0]);
        max_z = std::max(max_z, p.aabb_max[1]);
    }
    if (!std::isfinite(min_x) || !std::isfinite(min_z) ||
        !(max_x > min_x) || !(max_z > min_z)) {
        return false;
    }
    const float span_x = max_x - min_x;
    const float span_z = max_z - min_z;

    struct PatchTex {
        const EhfBgPatch*    patch = nullptr;
        std::vector<uint8_t> rgba;
        int                  w = 0, h = 0;
    };
    std::vector<PatchTex> decoded;
    decoded.reserve(parsed.bg_patches.size());
    std::vector<float> dens_x, dens_z;
    size_t n_nopages = 0, n_badhdr = 0, n_baddec = 0;
    for (const EhfBgPatch& p : parsed.bg_patches) {
        if (p.pages.empty()) { ++n_nopages; continue; }
        const uint32_t off = p.pages[0].first;
        const uint32_t len = p.pages[0].second;
        if (uint64_t(off) + len > ehf.size() || len < 0x60 ||
            ehf_be32(ehf, off) != 0xFFFFFFFEu) {
            ++n_badhdr;
            continue;
        }
        const uint32_t pf = ehf_be32(ehf, off + 0x18);
        if (pf != 35u) { ++n_badhdr; continue; }

        const uint32_t mt = ehf_be32(ehf, off + 0x20);
        if (mt < 0x54 || mt > 0x200 ||
            size_t(off) + mt + 8 > ehf.size()) { ++n_badhdr; continue; }
        const uint32_t comp_size = ehf_be32(ehf, off + mt + 4);
        size_t blob_end = size_t(off) + mt + 8 + comp_size;
        if (blob_end > ehf.size()) blob_end = ehf.size();
        std::vector<uint8_t> page(ehf.begin() + off, ehf.begin() + blob_end);
        TextureAtlas::DecodedAtlas dec = TextureAtlas::DecodeAtlas(page);
        if (!dec.ok || dec.rgba.empty()) { ++n_baddec; continue; }

        if (const char* dir = std::getenv("F2AB_DUMP_VISTA")) {
            static int s_pg = 0;
            if (s_pg < 4) {
                const std::string pp = std::string(dir) + "/page_" +
                    std::to_string(s_pg) + "_off" + std::to_string(off) +
                    "_" + std::to_string(dec.width) + "x" +
                    std::to_string(dec.height) + ".png";
                tex_export_png(pp, dec.rgba.data(), dec.width, dec.height);
                OutputLog::info("vista pages: dumped raw page " + pp +
                    " (blob " + std::to_string(blob_end - off) +
                    "B, comp " + std::to_string(comp_size) + ")");
                ++s_pg;
            }
        }

        PatchTex pt;
        pt.patch = &p;
        pt.rgba  = std::move(dec.rgba);
        pt.w     = dec.width;
        pt.h     = dec.height;
        const float sx = p.aabb_max[0] - p.aabb_min[0];
        const float sz = p.aabb_max[1] - p.aabb_min[1];
        if (sx > 0.0f && sz > 0.0f) {
            dens_x.push_back(float(pt.w) / sx);
            dens_z.push_back(float(pt.h) / sz);
        }
        decoded.push_back(std::move(pt));
    }
    if (decoded.empty() || dens_x.empty()) {
        OutputLog::warn("vista pages: no decodable pages (" +
                        std::to_string(parsed.bg_patches.size()) +
                        " patches: " + std::to_string(n_nopages) +
                        " without pages, " + std::to_string(n_badhdr) +
                        " bad header, " + std::to_string(n_baddec) +
                        " decode failed)");
        return false;
    }

    float f2 = 0.5f;
    if (ehf.size() >= 47) {
        f2 = read_be_f32_raw(ehf.data() + 43);
    }
    const float M = (std::isfinite(f2) && f2 > 0.0f) ? f2 * 16.0f : 8.0f;

    auto median = [](std::vector<float>& v) {
        std::sort(v.begin(), v.end());
        return v[v.size() / 2];
    };

    float density_x = std::clamp(median(dens_x), 0.5f, 32.0f);
    float density_z = std::clamp(median(dens_z), 0.5f, 32.0f);
    constexpr float kMaxDim = 4096.0f;
    if (span_x * density_x > kMaxDim) density_x = kMaxDim / span_x;
    if (span_z * density_z > kMaxDim) density_z = kMaxDim / span_z;

    out_w = std::max(4, int(std::lround(span_x * density_x)));
    out_h = std::max(4, int(std::lround(span_z * density_z)));
    out_rgba.assign(size_t(out_w) * size_t(out_h) * 4, 0);
    std::vector<uint8_t> filled(size_t(out_w) * size_t(out_h), 0);

    size_t blitted = 0;
    for (const PatchTex& pt : decoded) {
        const EhfBgPatch& p = *pt.patch;
        const int dx = int(std::lround((p.aabb_min[0] - min_x) /
                                       span_x * float(out_w)));
        const int dy = int(std::lround((p.aabb_min[1] - min_z) /
                                       span_z * float(out_h)));
        const int dw = int(std::lround((p.aabb_max[0] - p.aabb_min[0]) /
                                       span_x * float(out_w)));
        const int dh = int(std::lround((p.aabb_max[1] - p.aabb_min[1]) /
                                       span_z * float(out_h)));
        if (dw <= 0 || dh <= 0 || dx < 0 || dy < 0) continue;

        const float tiles_x = std::max(1.0f, (p.aabb_max[0] - p.aabb_min[0]) / M);
        const float tiles_z = std::max(1.0f, (p.aabb_max[1] - p.aabb_min[1]) / M);
        const int nx = std::max(1, int(std::lround(tiles_x)));
        const int nz = std::max(1, int(std::lround(tiles_z)));
        for (int tz = 0; tz < nz; ++tz) {
            for (int tx = 0; tx < nx; ++tx) {
                const int tdx = dx + int(std::lround(float(tx) / float(nx)
                                                     * float(dw)));
                const int tdy = dy + int(std::lround(float(tz) / float(nz)
                                                     * float(dh)));
                const int tdw = dx + int(std::lround(float(tx + 1) / float(nx)
                                                     * float(dw))) - tdx;
                const int tdh = dy + int(std::lround(float(tz + 1) / float(nz)
                                                     * float(dh))) - tdy;
                if (tdw <= 0 || tdh <= 0) continue;
                blit_resampled_rgba(pt.rgba, pt.w, pt.h,
                                    out_rgba, out_w, out_h,
                                    tdx, tdy, tdw, tdh);
            }
        }
        const int cw = std::min(dw, out_w - dx);
        const int ch = std::min(dh, out_h - dy);
        for (int y = 0; y < ch; ++y) {
            std::memset(filled.data() + size_t(dy + y) * out_w + dx, 1, cw);
        }
        ++blitted;
    }
    if (blitted == 0) {
        out_rgba.clear();
        out_w = 0;
        out_h = 0;
        return false;
    }

    for (int pass = 0; pass < 4; ++pass) {
        std::vector<uint8_t> next = filled;
        bool changed = false;
        for (int y = 0; y < out_h; ++y) {
            for (int x = 0; x < out_w; ++x) {
                const size_t i = size_t(y) * out_w + x;
                if (filled[i]) continue;
                static const int dxs[4] = { 1, -1, 0,  0 };
                static const int dys[4] = { 0,  0, 1, -1 };
                for (int k = 0; k < 4; ++k) {
                    const int nx = x + dxs[k];
                    const int ny = y + dys[k];
                    if (nx < 0 || ny < 0 || nx >= out_w || ny >= out_h) continue;
                    const size_t ni = size_t(ny) * out_w + nx;
                    if (!filled[ni]) continue;
                    std::memcpy(out_rgba.data() + i * 4,
                                out_rgba.data() + ni * 4, 4);
                    next[i] = 1;
                    changed = true;
                    break;
                }
            }
        }
        filled.swap(next);
        if (!changed) break;
    }

    if (const char* dir = std::getenv("F2AB_DUMP_VISTA")) {
        static int s_seq = 0;
        const std::string path = std::string(dir) + "/vista_comp_" +
                                 std::to_string(s_seq++) + ".png";
        tex_export_png(path, out_rgba.data(), out_w, out_h);
        OutputLog::info("vista pages: dumped composite to " + path);
    }

    std::ostringstream os;
    os << "vista_pages[" << blitted << "/" << parsed.bg_patches.size()
       << " patches, " << out_w << "x" << out_h << "]";
    out_name = os.str();
    OutputLog::success("ehf: vista page composite " + std::to_string(out_w) +
                       "x" + std::to_string(out_h) + " from " +
                       std::to_string(blitted) + "/" +
                       std::to_string(parsed.bg_patches.size()) +
                       " bg-map pages");
    return true;
}
