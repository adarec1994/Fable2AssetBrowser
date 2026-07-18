bool is_in_audio_folder(const std::string& path) {
    std::string lower_path = path;
    std::transform(lower_path.begin(), lower_path.end(), lower_path.begin(), ::tolower);
    return lower_path.find("/audio/") != std::string::npos ||
           lower_path.find("\\audio\\") != std::string::npos;
}
