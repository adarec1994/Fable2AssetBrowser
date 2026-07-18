bool load_toc(const std::filesystem::path& toc_path,
              std::vector<AnimClip>& out_clips) {
    out_clips.clear();
    std::ifstream f(toc_path, std::ios::binary | std::ios::ate);
    if (!f) {
        OutputLog::error("AnimBank: cannot open " + toc_path.string());
        return false;
    }
    std::streamsize sz = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> blob((size_t)sz);
    if (!f.read(reinterpret_cast<char*>(blob.data()), sz)) {
        OutputLog::error("AnimBank: read failed for " + toc_path.string());
        return false;
    }
    return load_toc_bytes(blob.data(), blob.size(), out_clips);
}

bool load_toc_for_root(const std::string& root,
                       std::vector<AnimClip>& out_clips) {
    out_clips.clear();

    constexpr const char* kRel = "data/animation/fable2_anims.animation_toc";

    if (ISO::IsoMount::instance().is_mounted()) {
        const ISO::MountedFile* mf =
            ISO::IsoMount::instance().find(kRel);
        if (!mf) {

            mf = ISO::IsoMount::instance().find_by_basename(
                "fable2_anims.animation_toc");
        }
        if (!mf) {
            OutputLog::warn("AnimBank: TOC not found in ISO");
            return false;
        }
        auto bytes = ISO::IsoMount::instance().read_file(mf->path);
        if (bytes.empty()) {
            OutputLog::error("AnimBank: failed to read TOC from ISO ("
                             + mf->path + ")");
            return false;
        }
        return load_toc_bytes(bytes.data(), bytes.size(), out_clips);
    }

    std::filesystem::path direct = std::filesystem::path(root) / kRel;
    if (std::filesystem::exists(direct)) {
        return load_toc(direct, out_clips);
    }
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(
             root, std::filesystem::directory_options::skip_permission_denied, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); ++it) {
        if (it->is_regular_file(ec) &&
            it->path().filename() == "fable2_anims.animation_toc") {
            return load_toc(it->path(), out_clips);
        }
    }
    OutputLog::warn("AnimBank: TOC not found under " + root);
    return false;
}
