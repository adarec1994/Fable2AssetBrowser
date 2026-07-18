uint32_t fnv1_model_path_hash(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    std::replace(s.begin(), s.end(), '/', '\\');

    uint32_t h = 0x811C9DC5u;
    for (unsigned char c : s) {
        h *= 0x01000193u;
        h ^= uint32_t(c);
    }
    return h;
}

std::string strip_model_suffixes(std::string s)
{
    auto strip = [](std::string& v, const char* suffix) {
        const size_t n = std::strlen(suffix);
        if (v.size() >= n && v.compare(v.size() - n, n, suffix) == 0) {
            v.resize(v.size() - n);
        }
    };
    strip(s, ".gmd");
    strip(s, ".mdl");
    return s;
}

std::string compact_match_key(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if (std::isalnum(c)) {
            out.push_back(char(std::tolower(c)));
        }
    }
    return out;
}

std::string model_name_from_path(const std::string& path)
{
    std::string p = path;
    std::replace(p.begin(), p.end(), '\\', '/');
    p = strip_model_suffixes(p);
    const size_t slash = p.find_last_of('/');
    return (slash == std::string::npos) ? p : p.substr(slash + 1);
}

bool read_be_f32_at(const std::vector<uint8_t>& bytes,
                    size_t off,
                    float& out)
{
    if (off + 4 > bytes.size()) return false;
    const uint32_t u =
        (uint32_t(bytes[off + 0]) << 24) |
        (uint32_t(bytes[off + 1]) << 16) |
        (uint32_t(bytes[off + 2]) << 8) |
         uint32_t(bytes[off + 3]);
    std::memcpy(&out, &u, sizeof(out));
    return std::isfinite(out);
}

std::string gmd_asset_key_from_raw_path(const std::string& raw_path)
{
    std::string p = lower_slash(raw_path);
    const std::string marker = "layout.instance.";
    if (const size_t pos = p.find(marker); pos != std::string::npos) {
        p.erase(0, pos + marker.size());
    }
    const size_t art = p.find("art/");
    if (art != std::string::npos) {
        p = p.substr(art);
    }
    const size_t slash = p.find_last_of('/');
    std::string name = (slash == std::string::npos) ? p : p.substr(slash + 1);
    auto strip_suffix = [](std::string& s, const char* suffix) {
        const size_t n = std::strlen(suffix);
        if (s.size() >= n && s.compare(s.size() - n, n, suffix) == 0) {
            s.resize(s.size() - n);
        }
    };
    strip_suffix(name, ".emdl");
    strip_suffix(name, ".mdl");
    strip_suffix(name, "_asset");
    strip_suffix(name, "asset");
    return compact_match_key(name);
}

bool parse_gmd_payload_transform(const std::vector<uint8_t>& bytes,
                                 size_t payload_start,
                                 size_t payload_end,
                                 Xform3f& out)
{
    struct Candidate {
        float score = std::numeric_limits<float>::infinity();
        float qx = 0.0f;
        float qy = 0.0f;
        float qz = 0.0f;
        float qw = 1.0f;
        float tx = 0.0f;
        float ty = 0.0f;
        float tz = 0.0f;
    };
    Candidate best;

    for (size_t align = 0; align < 4; ++align) {
        std::vector<float> floats;
        for (size_t off = payload_start + align;
             off + 4 <= payload_end;
             off += 4)
        {
            float f = 0.0f;
            if (!read_be_f32_at(bytes, off, f)) {
                floats.push_back(std::numeric_limits<float>::quiet_NaN());
            } else {
                floats.push_back(f);
            }
        }
        if (floats.size() < 7) continue;

        for (size_t i = 0; i + 7 <= floats.size(); ++i) {
            const float qx = floats[i + 0];
            const float qy = floats[i + 1];
            const float qz = floats[i + 2];
            const float qw = floats[i + 3];
            const float tx = floats[i + 4];
            const float ty = floats[i + 5];
            const float tz = floats[i + 6];
            const float vals[] = {qx, qy, qz, qw, tx, ty, tz};
            bool finite = true;
            for (float v : vals) {
                if (!std::isfinite(v)) {
                    finite = false;
                    break;
                }
            }
            if (!finite) continue;
            if (std::fabs(tx) > 512.0f || std::fabs(ty) > 512.0f ||
                std::fabs(tz) > 512.0f)
            {
                continue;
            }
            const float qmag =
                std::sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
            if (!std::isfinite(qmag) || qmag < 0.6f || qmag > 1.4f) {
                continue;
            }
            const float pos_mag =
                std::sqrt(tx * tx + ty * ty + tz * tz);
            const float score =
                std::fabs(qmag - 1.0f) * 100.0f +
                (pos_mag < 1e-4f ? 20.0f : 0.0f) +
                float(i) * 0.01f + float(align) * 0.001f;
            if (score < best.score) {
                best = {score, qx, qy, qz, qw, tx, ty, tz};
            }
        }
    }

    if (!std::isfinite(best.score)) {
        return false;
    }
    out.r = game_mat_to_xform_axes(
        mat3_from_quat(best.qx, best.qy, best.qz, best.qw));
    out.t = game_vec_to_xform_axes(best.tx, best.ty, best.tz);
    return true;
}

std::vector<GmdLayoutChild>
parse_gmd_layout_children(const std::vector<uint8_t>& bytes)
{
    std::vector<GmdLayoutChild> out;
    static constexpr const char* kMarkers[] = {
        "Prop.Layout.Instance.",
        "Light.Layout.Instance.",
        "Environment.Layout.Instance.",
    };
    size_t pos = 0;
    while (pos < bytes.size()) {
        auto best_it = bytes.end();
        const char* best_marker = nullptr;
        for (const char* marker : kMarkers) {
            const size_t marker_len = std::strlen(marker);
            if (pos + marker_len >= bytes.size()) continue;
            const auto it = std::search(
                bytes.begin() +
                    static_cast<std::vector<uint8_t>::difference_type>(pos),
                bytes.end(),
                marker,
                marker + marker_len);
            if (it != bytes.end() &&
                (best_it == bytes.end() || it < best_it))
            {
                best_it = it;
                best_marker = marker;
            }
        }
        const auto it = best_it;
        if (it == bytes.end()) break;
        (void)best_marker;
        const size_t start =
            static_cast<size_t>(std::distance(bytes.begin(), it));
        size_t str_end = start;
        while (str_end < bytes.size() && bytes[str_end] != 0) {
            ++str_end;
        }
        if (str_end >= bytes.size()) break;

        std::string raw(reinterpret_cast<const char*>(&bytes[start]),
                        str_end - start);
        size_t payload_start = str_end + 1;
        size_t payload_end = std::min(bytes.size(), payload_start + 160);
        for (size_t s = payload_start; s + 4 <= payload_end; ++s) {
            if (bytes[s + 0] == 0xff && bytes[s + 1] == 0xff &&
                bytes[s + 2] == 0xff && bytes[s + 3] == 0xff)
            {
                payload_end = s;
                break;
            }
        }

        GmdLayoutChild child;
        child.raw_path = raw;
        child.asset_key = gmd_asset_key_from_raw_path(raw);
        child.offset = start;
        if (!child.asset_key.empty() &&
            parse_gmd_payload_transform(
                bytes, payload_start, payload_end, child.local))
        {
            out.push_back(std::move(child));
        }
        pos = str_end + 1;
    }
    return out;
}

