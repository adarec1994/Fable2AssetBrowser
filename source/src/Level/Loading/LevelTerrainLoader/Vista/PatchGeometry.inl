bool build_ehf_vista_patch_geoms(const std::vector<uint8_t>& ehf,
                                 std::vector<Level::VistaPatchGeom>& out_geoms,
                                 std::string* out_stats)
{
    out_geoms.clear();
    if (out_stats) out_stats->clear();

    static constexpr char kMagic[] = "HeightFieldGraphicsFile";
    static constexpr size_t kMagicLen = sizeof(kMagic) - 1;
    static constexpr size_t kHeaderLen = 63;
    if (ehf.size() < kHeaderLen ||
        std::memcmp(ehf.data(), kMagic, kMagicLen) != 0) {
        return false;
    }
    const uint32_t body_off  = read_be_u32_raw(ehf.data() + 55);
    const uint32_t body_size = read_be_u32_raw(ehf.data() + 59);
    if (uint64_t(body_off) + uint64_t(body_size) > ehf.size()) return false;

    float f2 = read_be_f32_raw(ehf.data() + 43);
    const float M = (std::isfinite(f2) && f2 > 0.0f) ? f2 * 16.0f : 8.0f;

    BeReader r;
    r.p = ehf.data() + body_off;
    r.n = body_size;
    r.i = 0;
    if (!skip_ehf_tex_blob(r) || !skip_ehf_tex_blob(r)) return false;

    float max_height_hint = 0.0f;
    uint32_t patch_count = 0;
    if (!r.f32(max_height_hint) || !r.u32(patch_count)) return false;
    if (patch_count == 0 || patch_count > 4096) return false;

    auto sane = [](float v) {
        return std::isfinite(v) && std::fabs(v) < 1000000.0f;
    };
    auto qkey = [](float x, float y) -> uint64_t {
        const int32_t qx = int32_t(std::lround(double(x) * 4.0));
        const int32_t qy = int32_t(std::lround(double(y) * 4.0));
        return (uint64_t(uint32_t(qx)) << 32) | uint32_t(qy);
    };

    struct VP { float amin[3]; float amax[3]; uint32_t W, H; };
    std::vector<VP> patches;
    patches.reserve(patch_count);
    std::unordered_map<uint64_t, float> lattice, lattice_diag;

    for (uint32_t pi = 0; pi < patch_count; ++pi) {
        float a = 0.0f, b = 0.0f;
        uint32_t W = 0, H = 0;
        if (!r.f32(a) || !r.f32(b) || !r.u32(W) || !r.u32(H)) return false;
        if (W == 0 || H == 0 || W > 1024 || H > 1024) return false;
        const uint64_t cells = uint64_t(W) * uint64_t(H);
        if (cells > 65536 ||
            uint64_t(r.i) + cells * 160ull + 24ull > uint64_t(r.n)) {
            return false;
        }
        VP p;
        p.W = W;
        p.H = H;
        const size_t cell_base = r.i;
        const uint8_t* aabb = r.p + cell_base + size_t(cells) * 160u;
        for (int k = 0; k < 3; ++k) {
            p.amin[k] = read_be_f32_raw(aabb + size_t(k) * 4u);
            p.amax[k] = read_be_f32_raw(aabb + 12u + size_t(k) * 4u);
        }
        for (uint64_t ci = 0; ci < cells; ++ci) {
            const uint8_t* bv = r.p + cell_base + size_t(ci) * 160u + 64u + 12u;
            const float bx = read_be_f32_raw(bv + 0);
            const float by = read_be_f32_raw(bv + 4);
            const float bh = read_be_f32_raw(bv + 8);
            if (sane(bx) && sane(by) && sane(bh)) lattice[qkey(bx, by)] = bh;
            const uint8_t* dv = r.p + cell_base + size_t(ci) * 160u + 64u + 48u;
            const float dx = read_be_f32_raw(dv + 0);
            const float dy = read_be_f32_raw(dv + 4);
            const float dh = read_be_f32_raw(dv + 8);
            if (sane(dx) && sane(dy) && sane(dh)) {
                lattice_diag.emplace(qkey(dx, dy), dh);
            }
        }
        patches.push_back(p);
        r.i += size_t(cells) * 160u + 24u;
    }
    if (patches.empty() || lattice.empty()) return false;

    std::vector<uint16_t> uv_u, uv_v;
    uint32_t uvt_w = 0, uvt_h = 0;
    {
        float rec_f = 0.0f;
        uint32_t rec_n = 0;
        if (r.f32(rec_f) && r.u32(rec_n) && rec_n <= 100000 &&
            r.skip(size_t(rec_n) * 18u)) {
            for (int t = 0; t < 2; ++t) {
                if (uint64_t(r.i) + 0x60 > uint64_t(r.n)) break;
                const uint8_t* tb = r.p + r.i;
                if (read_be_u32_raw(tb) != 0xFFFFFFFEu) break;
                if (read_be_u32_raw(tb + 0x18) != 98u) break;
                const uint32_t tw = read_be_u32_raw(tb + 0x10);
                const uint32_t th = read_be_u32_raw(tb + 0x14);
                const uint32_t mt = read_be_u32_raw(tb + 0x20);
                if (tw == 0 || th == 0 || tw > 65536 || th > 65536 ||
                    mt < 0x24 || mt > 0x200 ||
                    uint64_t(r.i) + mt + uint64_t(tw) * th * 2u >
                        uint64_t(r.n)) {
                    break;
                }
                std::vector<uint16_t>& dst = (t == 0) ? uv_u : uv_v;
                if (t == 1 && (tw != uvt_w || th != uvt_h)) break;
                dst.resize(size_t(tw) * th);
                const uint8_t* px = tb + mt;
                for (size_t k = 0; k < dst.size(); ++k) {
                    dst[k] = uint16_t((px[k * 2] << 8) | px[k * 2 + 1]);
                }
                uvt_w = tw;
                uvt_h = th;
                r.i += size_t(mt) + size_t(tw) * th * 2u;
            }
        }
    }
    const bool have_uv_tables =
        !uv_u.empty() && uv_v.size() == uv_u.size() &&
        uvt_w >= 17 && (uvt_w % 17u) == 0;
    const float org_x = read_be_f32_raw(ehf.data() + 27);
    const float org_y = read_be_f32_raw(ehf.data() + 31);

    EhfParsedBody parsed;
    const bool have_pages = ParseEhfBody(ehf, parsed) &&
                            parsed.bg_patches.size() == patches.size();

    out_geoms.reserve(patches.size());
    size_t textured = 0;
    for (size_t pi = 0; pi < patches.size(); ++pi) {
        const VP& p = patches[pi];
        const float ax = p.amin[0];
        const float ay = p.amin[1];
        const float sx = (p.amax[0] - p.amin[0]) / float(p.W);
        const float sy = (p.amax[1] - p.amin[1]) / float(p.H);
        if (!(sx > 0.0f) || !(sy > 0.0f) ||
            !std::isfinite(sx) || !std::isfinite(sy)) {
            continue;
        }
        const uint32_t VW = p.W + 1;
        const uint32_t VH = p.H + 1;

        std::vector<float>  gh(size_t(VW) * size_t(VH), 0.0f);
        std::vector<uint8_t> ghas(size_t(VW) * size_t(VH), 0);
        for (uint32_t j = 0; j < VH; ++j) {
            for (uint32_t i = 0; i < VW; ++i) {
                const uint64_t key = qkey(ax + sx * float(i), ay + sy * float(j));
                float hv = 0.0f;
                bool have = false;
                if (auto it = lattice.find(key); it != lattice.end()) {
                    hv = it->second; have = true;
                } else if (auto it2 = lattice_diag.find(key);
                           it2 != lattice_diag.end()) {
                    hv = it2->second; have = true;
                }
                if (have) {
                    gh[size_t(j) * VW + i]   = hv;
                    ghas[size_t(j) * VW + i] = 1;
                }
            }
        }
        for (uint32_t j = 0; j < VH; ++j) {
            for (uint32_t i = 0; i < VW; ++i) {
                const size_t gi = size_t(j) * VW + i;
                if (ghas[gi]) continue;
                float best = p.amin[2];
                int best_d = INT32_MAX;
                for (uint32_t jj = 0; jj < VH; ++jj) {
                    for (uint32_t ii = 0; ii < VW; ++ii) {
                        const size_t oi = size_t(jj) * VW + ii;
                        if (!ghas[oi]) continue;
                        const int d = std::abs(int(ii) - int(i)) +
                                      std::abs(int(jj) - int(j));
                        if (d < best_d) { best_d = d; best = gh[oi]; }
                    }
                }
                gh[gi] = best;
            }
        }

        auto h_at = [&](int i, int j) -> float {
            const int ci = std::clamp(i, 0, int(VW) - 1);
            const int cj = std::clamp(j, 0, int(VH) - 1);
            return gh[size_t(cj) * VW + size_t(ci)];
        };
        auto n_at = [&](int i, int j, float out[3]) {
            const float hl = h_at(i - 1, j);
            const float hr = h_at(i + 1, j);
            const float hd = h_at(i, j - 1);
            const float hu = h_at(i, j + 1);
            float nx = (hl - hr) * sy;
            float ny = 2.0f * sx * sy;
            float nz = (hd - hu) * sx;
            const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (len > 1e-6f) { nx /= len; ny /= len; nz /= len; }
            else { nx = 0.0f; ny = 1.0f; nz = 0.0f; }
            out[0] = nx; out[1] = ny; out[2] = nz;
        };

        Level::VistaPatchGeom vg;
        TerrainMesh& m = vg.mesh;
        m.min_height =  std::numeric_limits<float>::infinity();
        m.max_height = -std::numeric_limits<float>::infinity();

        const int file_cells_w = have_uv_tables ? int(uvt_w / 17u) : 0;
        const int cgx0 = int(std::lround((ax - org_x) / M));
        const int cgy0 = int(std::lround((ay - org_y) / M));
        const bool cells_in_table =
            have_uv_tables && cgx0 >= 0 && cgy0 >= 0 &&
            cgx0 + int(p.W) <= file_cells_w &&
            cgy0 + int(p.H) <= int(uvt_h) &&
            uint64_t(p.W) * p.H <= 4096;

        if (cells_in_table) {

            const size_t vtx_per_cell = 17u * 17u;
            m.positions.reserve(size_t(p.W) * p.H * vtx_per_cell * 3);
            m.uvs.reserve(size_t(p.W) * p.H * vtx_per_cell * 2);
            m.normals.reserve(size_t(p.W) * p.H * vtx_per_cell * 3);
            for (uint32_t cj = 0; cj < p.H; ++cj) {
                for (uint32_t ci = 0; ci < p.W; ++ci) {
                    const size_t row = size_t(cgy0 + int(cj)) * uvt_w;
                    const uint16_t* us = &uv_u[row + size_t(cgx0 + int(ci)) * 17u];
                    const uint16_t* vs = &uv_v[row + size_t(cgx0 + int(ci)) * 17u];
                    const float h00 = h_at(int(ci),     int(cj));
                    const float h10 = h_at(int(ci) + 1, int(cj));
                    const float h01 = h_at(int(ci),     int(cj) + 1);
                    const float h11 = h_at(int(ci) + 1, int(cj) + 1);
                    float n00[3], n10[3], n01[3], n11[3];
                    n_at(int(ci),     int(cj),     n00);
                    n_at(int(ci) + 1, int(cj),     n10);
                    n_at(int(ci),     int(cj) + 1, n01);
                    n_at(int(ci) + 1, int(cj) + 1, n11);
                    const uint32_t base = uint32_t(m.positions.size() / 3);
                    for (int j2 = 0; j2 <= 16; ++j2) {
                        const float fy = float(j2) / 16.0f;
                        for (int i2 = 0; i2 <= 16; ++i2) {
                            const float fx = float(i2) / 16.0f;
                            const float wx = ax + sx * (float(ci) + fx);
                            const float wy = ay + sy * (float(cj) + fy);
                            const float wh =
                                (h00 * (1.0f - fx) + h10 * fx) * (1.0f - fy) +
                                (h01 * (1.0f - fx) + h11 * fx) * fy;
                            m.positions.push_back(wx);
                            m.positions.push_back(wh);
                            m.positions.push_back(wy);
                            m.uvs.push_back(float(us[i2]) / 65535.0f);
                            m.uvs.push_back(float(vs[j2]) / 65535.0f);
                            float nv[3];
                            for (int k = 0; k < 3; ++k) {
                                nv[k] =
                                    (n00[k] * (1.0f - fx) + n10[k] * fx) *
                                        (1.0f - fy) +
                                    (n01[k] * (1.0f - fx) + n11[k] * fx) * fy;
                            }
                            const float nl = std::sqrt(nv[0] * nv[0] +
                                                       nv[1] * nv[1] +
                                                       nv[2] * nv[2]);
                            if (nl > 1e-6f) {
                                nv[0] /= nl; nv[1] /= nl; nv[2] /= nl;
                            }
                            m.normals.push_back(nv[0]);
                            m.normals.push_back(nv[1]);
                            m.normals.push_back(nv[2]);
                            m.min_height = std::min(m.min_height, wh);
                            m.max_height = std::max(m.max_height, wh);
                        }
                    }
                    for (int j2 = 0; j2 < 16; ++j2) {
                        for (int i2 = 0; i2 < 16; ++i2) {
                            const uint32_t i00 = base + uint32_t(j2 * 17 + i2);
                            const uint32_t i10 = i00 + 1;
                            const uint32_t i01 = i00 + 17;
                            const uint32_t i11 = i01 + 1;
                            m.indices.push_back(i00);
                            m.indices.push_back(i01);
                            m.indices.push_back(i10);
                            m.indices.push_back(i10);
                            m.indices.push_back(i01);
                            m.indices.push_back(i11);
                        }
                    }
                }
            }
        } else {

            const float patch_w = p.amax[0] - p.amin[0];
            const float patch_h = p.amax[1] - p.amin[1];
            const float inv_pw = patch_w > 0.0f ? 1.0f / patch_w : 0.0f;
            const float inv_ph = patch_h > 0.0f ? 1.0f / patch_h : 0.0f;
            m.positions.reserve(size_t(VW) * VH * 3);
            m.uvs.reserve(size_t(VW) * VH * 2);
            m.normals.reserve(size_t(VW) * VH * 3);
            for (uint32_t j = 0; j < VH; ++j) {
                for (uint32_t i = 0; i < VW; ++i) {
                    const float wx = ax + sx * float(i);
                    const float wy = ay + sy * float(j);
                    const float wh = gh[size_t(j) * VW + i];
                    m.positions.push_back(wx);
                    m.positions.push_back(wh);
                    m.positions.push_back(wy);
                    m.uvs.push_back((wx - ax) * inv_pw);
                    m.uvs.push_back((wy - ay) * inv_ph);
                    m.min_height = std::min(m.min_height, wh);
                    m.max_height = std::max(m.max_height, wh);
                    float nv[3];
                    n_at(int(i), int(j), nv);
                    m.normals.push_back(nv[0]);
                    m.normals.push_back(nv[1]);
                    m.normals.push_back(nv[2]);
                }
            }
            for (uint32_t j = 0; j + 1 < VH; ++j) {
                for (uint32_t i = 0; i + 1 < VW; ++i) {
                    const uint32_t i00 = uint32_t(size_t(j) * VW + i);
                    const uint32_t i10 = i00 + 1;
                    const uint32_t i01 = uint32_t(size_t(j + 1) * VW + i);
                    const uint32_t i11 = i01 + 1;
                    m.indices.push_back(i00);
                    m.indices.push_back(i01);
                    m.indices.push_back(i10);
                    m.indices.push_back(i10);
                    m.indices.push_back(i01);
                    m.indices.push_back(i11);
                }
            }
        }
        if (!std::isfinite(m.min_height)) m.min_height = 0.0f;
        if (!std::isfinite(m.max_height)) m.max_height = 0.0f;
        m.width  = VW;
        m.height = VH;
        m.ok = !m.indices.empty();
        if (!m.ok) continue;

        if (have_pages && !parsed.bg_patches[pi].pages.empty()) {
            const uint32_t off = parsed.bg_patches[pi].pages[0].first;
            if (decode_bg_page(ehf, off, vg.page_rgba, vg.page_w, vg.page_h)) {
                ++textured;
            }
        }
        out_geoms.push_back(std::move(vg));
    }

    if (out_geoms.empty()) return false;
    if (out_stats) {
        std::ostringstream ss;
        ss << out_geoms.size() << " patch mesh(es), " << textured
           << " textured, M=" << M
           << (have_uv_tables ? ", cell-atlas uv tables" : ", NO uv tables");
        *out_stats = ss.str();
    }
    return true;
}
