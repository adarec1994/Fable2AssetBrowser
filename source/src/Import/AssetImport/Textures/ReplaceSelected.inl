bool replace_texture(const std::string& img_path,
                     const std::string& target_bnk_path,
                     int target_file_index, const Options& opt,
                     Result& res, std::string& err) {
    DebugLog::Scope debug_scope("Replace texture", img_path + " | " +
        target_bnk_path + " | index=" +
        std::to_string(target_file_index));
    res = Result{};
    TextureTargets target;
    if (!resolve_texture_targets(target_bnk_path, target_file_index,
                                 target, err)) {
        return false;
    }

    progress_update(10, 100, "Loading " +
        std::filesystem::path(img_path).filename().string());
    ImageLoad::Image decoded;
    if (!ImageLoad::load_file(img_path, decoded, err)) {
        return false;
    }

    TexWriter::Options texture_options;
    texture_options.max_dimension = opt.max_tex_dim;
    texture_options.generate_mips = opt.generate_mips;
    texture_options.format = opt.tex_format;
    if (texture_options.format == TexWriter::Format::Auto) {
        try {
            const std::vector<uint8_t> header = BnkCache::extract_bytes(
                target.header.path, target.header.index);
            if (header.size() >= 28) {
                const uint32_t format =
                    (uint32_t(header[24]) << 24) |
                    (uint32_t(header[25]) << 16) |
                    (uint32_t(header[26]) << 8) |
                    uint32_t(header[27]);
                if (format == 35) {
                    texture_options.format = TexWriter::Format::BC1;
                } else if (format == 39) {
                    texture_options.format = TexWriter::Format::BC3;
                } else if (format == 40) {
                    texture_options.format = TexWriter::Format::BC5Normal;
                } else if (format == 2 || format == 4) {
                    texture_options.format = TexWriter::Format::RawARGB;
                }
            }
        } catch (...) {
        }
    }

    progress_update(40, 100, "Encoding replacement texture");
    TexWriter::BuiltTex built;
    if (!TexWriter::build_from_rgba(
            decoded.rgba.data(), decoded.width, decoded.height,
            texture_options, built, err)) {
        return false;
    }
    if (!verify_tex(built, err)) {
        return false;
    }

    progress_update(65, 100, "Checking backup coverage");
    if (!apply_texture_replacement(target, built, err)) {
        return false;
    }

    progress_update(95, 100, "Refreshing texture caches");
    res.tex_virtual_paths.push_back(target.virtual_path);
    res.notes.push_back("replaced " + target.virtual_path + " (" +
                        std::to_string(built.width) + "x" +
                        std::to_string(built.height) + ", " +
                        std::to_string(built.mip_count) + " mips)");
    debug_scope.Result("success | " + target.virtual_path);
    return true;
}
