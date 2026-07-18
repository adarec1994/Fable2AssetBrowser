extern std::vector<std::string> scan_luas_recursive(const std::string& root);

static std::string determine_bank_name(const std::string& path) {
    std::vector<std::string> banks = {
        "gamescripts\\scripts\\",
        "gamescripts_r\\scripts\\",
        "guiscripts\\scripts\\",
        "guiscripts\\art\\"
    };
    std::string path_lower = path;
    std::transform(path_lower.begin(), path_lower.end(), path_lower.begin(), ::tolower);

    for (const auto& bank : banks) {
        std::string bank_lower = bank;
        std::transform(bank_lower.begin(), bank_lower.end(), bank_lower.begin(), ::tolower);
        if (path_lower.find(bank_lower) != std::string::npos) {
            return bank;
        }
    }
    return "";
}

static std::string build_output_path(const std::string& input_path, const std::string& bank_name) {
    if (bank_name.empty()) {
        return "";
    }

    std::string path_lower = input_path;
    std::transform(path_lower.begin(), path_lower.end(), path_lower.begin(), ::tolower);
    std::string bank_lower = bank_name;
    std::transform(bank_lower.begin(), bank_lower.end(), bank_lower.begin(), ::tolower);

    size_t pos = path_lower.find(bank_lower);
    if (pos == std::string::npos) {
        return "";
    }

    std::string before = input_path.substr(0, pos + bank_name.length());
    std::string after = input_path.substr(pos + bank_name.length());

#ifdef _WIN32
    std::string result = before + "decompiled\\" + after;
    size_t double_sep;
    while ((double_sep = result.find("\\\\")) != std::string::npos) {
        result.replace(double_sep, 2, "\\");
    }
#else
    std::string result = before + "decompiled/" + after;
    size_t double_sep;
    while ((double_sep = result.find("//")) != std::string::npos) {
        result.replace(double_sep, 2, "/");
    }
#endif

    return result;
}

static bool copy_file_simple(const std::string& src, const std::string& dst) {
    std::ifstream in(src, std::ios::binary);
    if (!in) return false;

    std::ofstream out(dst, std::ios::binary);
    if (!out) return false;

    out << in.rdbuf();
    return true;
}
