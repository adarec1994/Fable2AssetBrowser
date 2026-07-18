static void asset_export_audio_raw_xma(const std::string& bnk_path,
                                       int file_index,
                                       const std::string& file_name)
{
    auto out = build_export_target(file_name);
    out.replace_extension(".xma");
    std::error_code ec;
    if (auto parent = out.parent_path(); !parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            OutputLog::error(std::string(".xma export: cannot create ") +
                             parent.string() + " - " + ec.message());
            return;
        }
    }
    bool ok = false;
    try {
        extract_one(bnk_path, file_index, out.string());
        ok = std::filesystem::exists(out, ec) && !ec;
    } catch (const std::exception& ex) {
        OutputLog::error(std::string(".xma export exception on ") +
                         file_name + ": " + ex.what());
    } catch (...) {
        OutputLog::error(std::string(".xma export exception on ") +
                         file_name);
    }
    if (ok) {
        OutputLog::success(std::string("Exported ") +
                           std::filesystem::path(file_name).filename().string()
                           + " as raw .xma -> " + out.string());
    } else {
        OutputLog::error(std::string(".xma export failed: ") + file_name);
    }
}
