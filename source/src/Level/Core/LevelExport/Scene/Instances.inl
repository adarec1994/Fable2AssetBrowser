std::array<float, 16> instance_matrix(const PropInstance& inst)
{
    const float px = inst.values[0];
    const float py = inst.values[2];
    const float pz = inst.values[1];
    std::array<float, 16> m{};
    if (inst.has_full_transform) {
        float scale = inst.values[12];
        if (!std::isfinite(scale) || scale == 0.0f) scale = 1.0f;
        const float* r = &inst.values[3];
        m = {
            r[0] * scale, r[1] * scale, r[2] * scale, px,
            r[3] * scale, r[4] * scale, r[5] * scale, py,
            r[6] * scale, r[7] * scale, r[8] * scale, pz,
            0.0f, 0.0f, 0.0f, 1.0f
        };
        return m;
    }

    const float s = inst.values[6];
    const float c = inst.values[7];
    const float sx = inst.values[9]  == 0.0f ? 1.0f : inst.values[9];
    const float sy = inst.values[10] == 0.0f ? sx : inst.values[10];
    const float sz = inst.values[11] == 0.0f ? sx : inst.values[11];
    m = {
        c * sx, 0.0f, s * sy, px,
        0.0f, sz, 0.0f, py,
        -s * sx, 0.0f, c * sy, pz,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    return m;
}

std::string model_output_name(const std::string& source,
                              const char* ext)
{
    return sanitize_name(stem_of(source)) + "_" + hex8(fnv1a64(source)) + ext;
}

const char* terrain_texture_extension(MdlTexExport::Format fmt)
{
    switch (fmt) {
        case MdlTexExport::Format::DDS: return ".dds";
        case MdlTexExport::Format::PNG: return ".png";
        case MdlTexExport::Format::JPG: return ".jpg";
    }
    return ".dds";
}

bool export_one_terrain_texture(const std::string& tex_name,
                                const std::string& preferred_bnk,
                                const std::filesystem::path& texture_dir,
                                const std::string& texture_rel_root,
                                std::unordered_map<std::string, std::string>& cache,
                                std::string& out_rel)
{
    out_rel.clear();
    if (tex_name.empty()) return false;
    const std::string key = lower_copy(to_slash(tex_name));
    auto hit = cache.find(key);
    if (hit != cache.end()) {
        out_rel = hit->second;
        return true;
    }

    const auto fmt = MdlTexExport::Format::PNG;
    const std::string leaf =
        sanitize_name(stem_of(tex_name)) + "_" + hex8(fnv1a64(key)) +
        terrain_texture_extension(fmt);
    const auto path = texture_dir / leaf;
    out_rel = texture_rel_root + "/" + leaf;
    if (file_exists_nonempty(path)) {
        cache[key] = out_rel;
        return true;
    }

    std::vector<unsigned char> tex_buf;
    if (!build_any_tex_buffer_for_name(tex_name, tex_buf, preferred_bnk)) {
        return false;
    }
    MdlTexExport::EncodedTex enc;
    if (!MdlTexExport::encode_largest_mip(tex_buf, fmt, enc) ||
        enc.bytes.empty()) {
        return false;
    }

    std::string err;
    if (!write_bytes(path, enc.bytes, err)) {
        OutputLog::warn("level export: terrain texture write failed: " + err);
        return false;
    }
    cache[key] = out_rel;
    return true;
}
