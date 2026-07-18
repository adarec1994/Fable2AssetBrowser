static std::string apply_folder_prefix_to_filename(const std::string& full_path, const std::string& extension) {
    std::string lower_path = full_path;
    std::transform(lower_path.begin(), lower_path.end(), lower_path.begin(), ::tolower);

    std::filesystem::path p(full_path);
    std::string filename = p.stem().string();
    std::string filename_lower = filename;
    std::transform(filename_lower.begin(), filename_lower.end(), filename_lower.begin(), ::tolower);

    if (filename_lower != "interior" && filename_lower != "exterior") {
        return filename + extension;
    }

    std::filesystem::path parent = p.parent_path();
    if (parent.empty()) {
        return filename + extension;
    }

    std::string folder_name = parent.filename().string();

    std::string result = folder_name + "_" + filename + extension;

    return result;
}
