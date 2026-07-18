bool import_image(const std::string& img_path, const Options& opt,
                  Result& res, std::string& err)
{
    DebugLog::Scope debug_scope("Import texture", img_path);
    res = Result{};
    const std::string stem =
        std::filesystem::path(img_path).stem().string();
    const std::string name =
        sanitize_name(opt.asset_name.empty() ? stem : opt.asset_name);
    std::string folder = normalize_folder(
        opt.dest_folder.empty() ? "art\\imported" : opt.dest_folder);

    progress_update(10, 100, "Loading " + stem);
    ImageLoad::Image decoded;
    if (!ImageLoad::load_file(img_path, decoded, err)) return false;

    TexWriter::Options topt;
    topt.max_dimension = opt.max_tex_dim;
    topt.generate_mips = opt.generate_mips;
    topt.format = opt.tex_format;
    TexWriter::BuiltTex built;
    progress_update(40, 100, "Encoding " + name);
    if (!TexWriter::build_from_rgba(decoded.rgba.data(), decoded.width,
                                    decoded.height, topt, built, err))
        return false;
    if (!verify_tex(built, err)) return false;

    const std::string vpath = folder + "\\" + name + ".tex";
    std::vector<PendingEntry> pending;
    queue_tex(vpath, built, pending);

    progress_update(70, 100, "Injecting into BNKs");
    std::vector<std::pair<std::string, std::string>> injected;
    if (!inject_entries(pending, injected, err)) return false;
    register_injected(injected);

    res.tex_virtual_paths.push_back(vpath);
    res.notes.push_back("texture " + vpath + " (" +
                        std::to_string(built.width) + "x" +
                        std::to_string(built.height) + ", " +
                        std::to_string(built.mip_count) + " mips)");
    debug_scope.Result("success | " + vpath);
    return true;
}
