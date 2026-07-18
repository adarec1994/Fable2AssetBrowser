std::string read_lua_file_content(const std::string& path) {

    auto bytes = read_all_bytes(std::filesystem::path(path));
    if (bytes.empty()) {
        return "-- Error: Could not open file";
    }
    if (bytes.size() > 10 * 1024 * 1024) {
        return "-- Error: File too large to preview (>10MB)";
    }

    bool is_bytecode = false;
    if (bytes.size() >= 4) {
        if (bytes[0] == 0x1B && bytes[1] == 'L' && bytes[2] == 'u' && bytes[3] == 'a') {
            is_bytecode = true;
        }
    }

    if (!is_bytecode) {
        return std::string(bytes.begin(), bytes.end());
    }

    return decompile_lua51_bytecode(bytes.data(), bytes.size());
}
