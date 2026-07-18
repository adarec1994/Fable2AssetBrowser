                        auto norm_path = [](std::string s) {
                            std::replace(s.begin(), s.end(), '\\', '/');
                            std::transform(s.begin(), s.end(), s.begin(),
                                [](unsigned char c) { return (char)std::tolower(c); });
                            return s;
                        };
                        auto with_ext = [](std::string p, const char* ext) {
                            const size_t slash = p.find_last_of("/\\");
                            const size_t dot = p.find_last_of('.');
                            if (dot != std::string::npos &&
                                (slash == std::string::npos || dot > slash)) {
                                p.resize(dot);
                            }
                            p += ext;
                            return p;
                        };
                        auto path_leaf = [&](const std::string& p) {
                            const std::string n = norm_path(p);
                            const size_t slash = n.find_last_of('/');
                            return (slash == std::string::npos)
                                ? n
                                : n.substr(slash + 1);
                        };
                        auto has_ext = [&](const std::string& p,
                                           const char* ext) {
                            const std::string n = norm_path(p);
                            const size_t len = std::strlen(ext);
                            return n.size() >= len &&
                                n.compare(n.size() - len, len, ext) == 0;
                        };
                        auto heightfield_id = [&](const std::string& p) {
                            const std::string n = norm_path(p);
                            const size_t id_pos = n.rfind("_id_");
                            if (id_pos == std::string::npos) {
                                return std::string{};
                            }
                            size_t first = id_pos + 4;
                            size_t last = first;
                            while (last < n.size()) {
                                const unsigned char c =
                                    static_cast<unsigned char>(n[last]);
                                if (!std::isxdigit(c)) break;
                                ++last;
                            }
                            return last > first ? n.substr(first, last - first)
                                                : std::string{};
                        };
                        auto path_overlap_score =
                            [&](const std::string& a,
                                const std::string& b) {
                                const std::string an = norm_path(a);
                                const std::string bn = norm_path(b);
                                int score = 0;
                                size_t start = 0;
                                while (start < an.size()) {
                                    const size_t end = an.find('/', start);
                                    const std::string part = an.substr(
                                        start,
                                        end == std::string::npos
                                            ? std::string::npos
                                            : end - start);
                                    if (part.size() > 2 &&
                                        bn.find(part) != std::string::npos) {
                                        ++score;
                                    }
                                    if (end == std::string::npos) break;
                                    start = end + 1;
                                }
                                return score;
                            };
                        auto resolve_adjacent_ghf =
                            [&](const std::string& ehf_path) {
                                const std::string exact =
                                    with_ext(ehf_path, ".ghf");
                                if (const FlatAssetEntry* fe =
                                        Level::FindHeightfieldByPath(exact)) {
                                    return fe->full_path.empty()
                                        ? exact
                                        : fe->full_path;
                                }

                                const std::string exact_leaf =
                                    path_leaf(exact);
                                const std::string id =
                                    heightfield_id(ehf_path);
                                const FlatAssetEntry* best = nullptr;
                                int best_score = 0;
                                for (const auto& fe : S.all_heightfield_files) {
                                    const std::string full =
                                        fe.full_path.empty()
                                            ? fe.name
                                            : fe.full_path;
                                    if (!has_ext(full, ".ghf") &&
                                        !has_ext(fe.name, ".ghf")) {
                                        continue;
                                    }

                                    int score = 0;
                                    if (path_leaf(full) == exact_leaf ||
                                        path_leaf(fe.name) == exact_leaf) {
                                        score += 1000;
                                    }
                                    if (!id.empty()) {
                                        const std::string cand_id =
                                            heightfield_id(full.empty()
                                                ? fe.name
                                                : full);
                                        if (cand_id == id) score += 700;
                                    }
                                    if (score == 0) continue;
                                    score += path_overlap_score(ehf_path,
                                                               full);
                                    if (score > best_score) {
                                        best_score = score;
                                        best = &fe;
                                    }
                                }
                                return best
                                    ? (best->full_path.empty()
                                        ? best->name
                                        : best->full_path)
                                    : std::string{};
                            };
                        auto build_ehf_proxy_mesh =
                            [&](const HeightfieldFiles& src,
                                float fallback_height,
                                TerrainMesh& out) {
                                out = {};
                                EhfParsedBody body;
                                if (!ParseEhfBody(src.ehf_bytes, body) ||
                                    body.chunks.empty() ||
                                    body.chunk_w == 0 ||
                                    body.chunk_h == 0) {
                                    return false;
                                }

                                float min_x = std::numeric_limits<float>::infinity();
                                float min_z = std::numeric_limits<float>::infinity();
                                float max_x = -std::numeric_limits<float>::infinity();
                                float max_z = -std::numeric_limits<float>::infinity();
                                for (const auto& c : body.chunks) {
                                    if (!std::isfinite(c.origin[0]) ||
                                        !std::isfinite(c.origin[1]) ||
                                        !std::isfinite(c.extent[0]) ||
                                        !std::isfinite(c.extent[1])) {
                                        continue;
                                    }
                                    min_x = std::min(min_x, c.origin[0]);
                                    min_z = std::min(min_z, c.origin[1]);
                                    max_x = std::max(max_x, c.extent[0]);
                                    max_z = std::max(max_z, c.extent[1]);
                                }
                                if (!std::isfinite(min_x) ||
                                    !std::isfinite(min_z) ||
                                    !std::isfinite(max_x) ||
                                    !std::isfinite(max_z) ||
                                    max_x <= min_x || max_z <= min_z) {
                                    return false;
                                }

                                uint32_t W = src.ehf_header.u0;
                                uint32_t H = src.ehf_header.u1;
                                if (W < 2 || H < 2 ||
                                    uint64_t(W) * uint64_t(H) > 600000ull) {
                                    W = body.chunk_w + 1;
                                    H = body.chunk_h + 1;
                                }
                                if (W < 2 || H < 2) return false;

                                const uint32_t CW = body.chunk_w;
                                const uint32_t CH = body.chunk_h;
                                const size_t corner_count =
                                    size_t(CW + 1) * size_t(CH + 1);
                                std::vector<float> corner_sum(
                                    corner_count, 0.0f);
                                std::vector<uint32_t> corner_count_hits(
                                    corner_count, 0);
                                auto corner_index =
                                    [&](uint32_t x, uint32_t y) {
                                        return size_t(y) * size_t(CW + 1) + x;
                                    };
                                const float chunk_span_x =
                                    (max_x - min_x) / float(CW);
                                const float chunk_span_z =
                                    (max_z - min_z) / float(CH);
                                auto add_corner =
                                    [&](uint32_t x, uint32_t y, float h) {
                                        const size_t ci = corner_index(x, y);
                                        corner_sum[ci] += h;
                                        ++corner_count_hits[ci];
                                    };
                                for (const auto& c : body.chunks) {
                                    int cx = int(std::lround(
                                        (c.origin[0] - min_x) /
                                        std::max(chunk_span_x, 1e-6f)));
                                    int cy = int(std::lround(
                                        (c.origin[1] - min_z) /
                                        std::max(chunk_span_z, 1e-6f)));
                                    cx = std::clamp(cx, 0, int(CW) - 1);
                                    cy = std::clamp(cy, 0, int(CH) - 1);

                                    float h = fallback_height;
                                    if (std::isfinite(c.origin[2]) &&
                                        std::isfinite(c.extent[2])) {
                                        h = 0.5f * (c.origin[2] + c.extent[2]);
                                    } else if (std::isfinite(c.origin[2])) {
                                        h = c.origin[2];
                                    } else if (std::isfinite(c.extent[2])) {
                                        h = c.extent[2];
                                    }

                                    const uint32_t ux = uint32_t(cx);
                                    const uint32_t uy = uint32_t(cy);
                                    add_corner(ux,     uy,     h);
                                    add_corner(ux + 1, uy,     h);
                                    add_corner(ux,     uy + 1, h);
                                    add_corner(ux + 1, uy + 1, h);
                                }

                                std::vector<float> corner_h(corner_count,
                                                            fallback_height);
                                for (size_t i = 0; i < corner_count; ++i) {
                                    if (corner_count_hits[i] > 0) {
                                        corner_h[i] = corner_sum[i] /
                                            float(corner_count_hits[i]);
                                    }
                                }
                                auto corner_h_at =
                                    [&](uint32_t x, uint32_t y) {
                                        x = std::min(x, CW);
                                        y = std::min(y, CH);
                                        return corner_h[corner_index(x, y)];
                                    };
                                auto bilerp_h =
                                    [&](float fx, float fy) {
                                        const int ix = std::clamp(
                                            int(std::floor(fx)), 0,
                                            int(CW) - 1);
                                        const int iy = std::clamp(
                                            int(std::floor(fy)), 0,
                                            int(CH) - 1);
                                        const float tx = std::clamp(
                                            fx - float(ix), 0.0f, 1.0f);
                                        const float ty = std::clamp(
                                            fy - float(iy), 0.0f, 1.0f);
                                        const float h00 = corner_h_at(
                                            uint32_t(ix), uint32_t(iy));
                                        const float h10 = corner_h_at(
                                            uint32_t(ix + 1), uint32_t(iy));
                                        const float h01 = corner_h_at(
                                            uint32_t(ix), uint32_t(iy + 1));
                                        const float h11 = corner_h_at(
                                            uint32_t(ix + 1), uint32_t(iy + 1));
                                        const float hx0 = h00 + (h10 - h00) * tx;
                                        const float hx1 = h01 + (h11 - h01) * tx;
                                        return hx0 + (hx1 - hx0) * ty;
                                    };

                                const size_t N = size_t(W) * size_t(H);
                                out.width = W;
                                out.height = H;
                                out.positions.resize(N * 3);
                                out.normals.resize(N * 3);
                                out.uvs.resize(N * 2);
                                out.min_height =
                                    std::numeric_limits<float>::infinity();
                                out.max_height =
                                    -std::numeric_limits<float>::infinity();

                                for (uint32_t y = 0; y < H; ++y) {
                                    const float vy = (H > 1)
                                        ? float(y) / float(H - 1)
                                        : 0.0f;
                                    const float fcy = vy * float(CH);
                                    for (uint32_t x = 0; x < W; ++x) {
                                        const float vx = (W > 1)
                                            ? float(x) / float(W - 1)
                                            : 0.0f;
                                        const float fcx = vx * float(CW);
                                        const float ph =
                                            bilerp_h(fcx, fcy);
                                        const size_t i = size_t(y) * W + x;
                                        out.positions[i * 3 + 0] =
                                            min_x + vx * (max_x - min_x);
                                        out.positions[i * 3 + 1] = ph;
                                        out.positions[i * 3 + 2] =
                                            min_z + vy * (max_z - min_z);
                                        out.uvs[i * 2 + 0] = vx;
                                        out.uvs[i * 2 + 1] = vy;
                                        out.min_height =
                                            std::min(out.min_height, ph);
                                        out.max_height =
                                            std::max(out.max_height, ph);
                                    }
                                }

                                const float step_x =
                                    (max_x - min_x) /
                                    float(std::max<uint32_t>(1, W - 1));
                                const float step_z =
                                    (max_z - min_z) /
                                    float(std::max<uint32_t>(1, H - 1));
                                auto height_at =
                                    [&](int x, int y) {
                                        x = std::clamp(x, 0, int(W) - 1);
                                        y = std::clamp(y, 0, int(H) - 1);
                                        return out.positions[
                                            (size_t(y) * W + size_t(x)) * 3 + 1];
                                    };
                                for (uint32_t y = 0; y < H; ++y) {
                                    for (uint32_t x = 0; x < W; ++x) {
                                        const float hl = height_at(
                                            int(x) - 1, int(y));
                                        const float hr = height_at(
                                            int(x) + 1, int(y));
                                        const float hd = height_at(
                                            int(x), int(y) - 1);
                                        const float hu = height_at(
                                            int(x), int(y) + 1);
                                        float nx = (hl - hr) * step_z;
                                        float ny = 2.0f * step_x * step_z;
                                        float nz = (hd - hu) * step_x;
                                        float len =
                                            std::sqrt(nx * nx + ny * ny +
                                                      nz * nz);
                                        if (len > 1e-6f) {
                                            nx /= len;
                                            ny /= len;
                                            nz /= len;
                                        } else {
                                            nx = 0.0f;
                                            ny = 1.0f;
                                            nz = 0.0f;
                                        }
                                        const size_t i = size_t(y) * W + x;
                                        out.normals[i * 3 + 0] = nx;
                                        out.normals[i * 3 + 1] = ny;
                                        out.normals[i * 3 + 2] = nz;
                                    }
                                }

                                out.indices.resize(
                                    size_t(W - 1) * size_t(H - 1) * 6);
                                size_t k = 0;
                                for (uint32_t y = 0; y + 1 < H; ++y) {
                                    for (uint32_t x = 0; x + 1 < W; ++x) {
                                        const uint32_t i00 =
                                            uint32_t(size_t(y) * W + x);
                                        const uint32_t i10 =
                                            uint32_t(size_t(y) * W + x + 1);
                                        const uint32_t i01 =
                                            uint32_t(size_t(y + 1) * W + x);
                                        const uint32_t i11 =
                                            uint32_t(size_t(y + 1) * W + x + 1);
                                        out.indices[k++] = i00;
                                        out.indices[k++] = i01;
                                        out.indices[k++] = i10;
                                        out.indices[k++] = i10;
                                        out.indices[k++] = i01;
                                        out.indices[k++] = i11;
                                    }
                                }
                                out.ok = true;
                                return true;
                            };
