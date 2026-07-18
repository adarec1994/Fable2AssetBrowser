static void asset_export_to_export_dir(const std::string& bnk_path,
                                       int file_index, bool ,
                                       const std::string& file_name,
                                       bool convert_audio = true)
{
    auto out = build_export_target(file_name);
    std::error_code ec;
    if (auto parent = out.parent_path(); !parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            OutputLog::error(std::string("Export: cannot create ") +
                             parent.string() + " - " + ec.message());
            return;
        }
    }

    bool ok = false;
    try {
        if (ext_is(file_name, ".mdl")) {
            std::vector<unsigned char> buf;
            if (reconstruct_mdl_paired(bnk_path, file_index, buf) &&
                !buf.empty()) {
                std::ofstream f(out, std::ios::binary | std::ios::trunc);
                if (f) {
                    f.write(reinterpret_cast<const char*>(buf.data()),
                            (std::streamsize)buf.size());
                    ok = f.good();
                }
            }
        } else {

            BNKItemUI item{};
            item.index = file_index;
            item.name  = file_name;
            item.size  = 0;
            extract_file_one(bnk_path, item,
                             out.parent_path().string(),
                             convert_audio);

            ok = std::filesystem::exists(out, ec) && !ec;
        }
    } catch (const std::exception& ex) {
        OutputLog::error(std::string("Export exception on ") + file_name +
                         ": " + ex.what());
        ok = false;
    } catch (...) {
        OutputLog::error(std::string("Export exception on ") + file_name);
        ok = false;
    }

    if (ok) {
        OutputLog::success(std::string("Exported ") +
                           std::filesystem::path(file_name).filename().string() +
                           " -> " + out.string());
    } else {
        OutputLog::error(std::string("Export failed: ") + file_name +
                         " (target: " + out.string() + ")");
    }
}
