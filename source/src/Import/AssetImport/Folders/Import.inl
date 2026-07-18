bool import_folder(const std::string& folder_path, const Options& opt,
                   Result& res, std::string& err)
{
    res = Result{};
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(folder_path, ec)) {
        err = "not a folder: " + folder_path;
        return false;
    }

    std::vector<std::string> glbs, images;
    for (const auto& de : fs::directory_iterator(folder_path, ec)) {
        if (!de.is_regular_file()) continue;
        const std::string p = de.path().string();
        std::string ext = to_lower(de.path().extension().string());
        if (ext == ".glb") glbs.push_back(p);
        else if (ImageLoad::extension_supported(p)) images.push_back(p);
    }
    if (glbs.empty() && images.empty()) {
        err = "no .glb or image files found in " + folder_path;
        return false;
    }

    int item = 0;
    const int total = (int)(glbs.size() + images.size());
    size_t failures = 0;
    for (const auto& g : glbs) {
        progress_update(item * 100 / total, 100,
                        fs::path(g).filename().string());
        Options one = opt;
        one.asset_name.clear();
        one.entity_id.clear();
        one.dest_folder = opt.dest_folder;
        Result r;
        std::string e;
        if (import_glb(g, one, r, e)) {
            res.notes.push_back("imported " + fs::path(g).filename().string());
            for (auto& n : r.notes) res.notes.push_back("  " + n);
            res.meshes += r.meshes;
            res.vertices += r.vertices;
            res.triangles += r.triangles;
            if (res.mdl_virtual_path.empty())
                res.mdl_virtual_path = r.mdl_virtual_path;
            if (r.gdb_template_created) {
                res.gdb_template_created = true;
                if (res.entity_id.empty()) res.entity_id = r.entity_id;
            }
            res.tex_virtual_paths.insert(res.tex_virtual_paths.end(),
                                         r.tex_virtual_paths.begin(),
                                         r.tex_virtual_paths.end());
        } else {
            ++failures;
            res.notes.push_back("FAILED " + fs::path(g).filename().string() +
                                ": " + e);
        }
        ++item;
    }
    for (const auto& im : images) {
        progress_update(item * 100 / total, 100,
                        fs::path(im).filename().string());
        Options one = opt;
        one.asset_name.clear();
        Result r;
        std::string e;
        if (import_image(im, one, r, e)) {
            res.notes.push_back("imported " + fs::path(im).filename().string());
            res.tex_virtual_paths.insert(res.tex_virtual_paths.end(),
                                         r.tex_virtual_paths.begin(),
                                         r.tex_virtual_paths.end());
        } else {
            ++failures;
            res.notes.push_back("FAILED " + fs::path(im).filename().string() +
                                ": " + e);
        }
        ++item;
    }

    if (failures == glbs.size() + images.size()) {
        err = "every file in the folder failed to import";
        return false;
    }
    return true;
}
