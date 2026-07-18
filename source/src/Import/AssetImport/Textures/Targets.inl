std::string normalized_path(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    std::replace(value.begin(), value.end(), '\\', '/');
    return value;
}

int texture_bank_role(const std::string& path) {
    const std::string leaf = to_lower(
        std::filesystem::path(path).filename().string());
    const bool header = leaf.find("header") != std::string::npos &&
                        leaf.find("texture") != std::string::npos;
    const bool mip0 = leaf.find("1024mip0") != std::string::npos &&
                      leaf.find("texture") != std::string::npos;
    const bool body = leaf.find("texture") != std::string::npos &&
                      !header && !mip0;
    if (header) return 1;
    if (mip0) return 2;
    if (body) return 3;
    return 4;
}

std::string texture_bank_family(const std::string& path) {
    std::string leaf = to_lower(
        std::filesystem::path(path).filename().string());
    if (leaf.size() >= 4 &&
        leaf.compare(leaf.size() - 4, 4, ".bnk") == 0) {
        leaf.resize(leaf.size() - 4);
    }
    for (const std::string suffix :
         {"_texture_headers", "_textures", "_texture", "_headers"}) {
        if (leaf.size() > suffix.size() &&
            leaf.compare(leaf.size() - suffix.size(), suffix.size(),
                         suffix) == 0) {
            leaf.resize(leaf.size() - suffix.size());
            break;
        }
    }
    return leaf;
}

struct TexturePart {
    std::string path;
    int index = -1;
    int rank = 1000;
};

struct TextureTargets {
    std::string virtual_path;
    TexturePart header;
    TexturePart mip0;
    TexturePart body;
};
