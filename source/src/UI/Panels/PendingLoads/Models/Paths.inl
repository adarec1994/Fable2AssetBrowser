static std::string normalized_asset_path(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

static std::string asset_leaf(std::string s)
{
    const size_t sl = s.find_last_of("/\\");
    if (sl != std::string::npos) s = s.substr(sl + 1);
    return s;
}

static bool is_shell_pair_model_path(const std::string& model_path)
{
    std::string leaf = normalized_asset_path(asset_leaf(model_path));
    return leaf == "exterior.mdl" || leaf == "interior.mdl";
}

static std::string asset_parent_key(const std::string& bnk_path)
{
    auto it = S.nested_bnk_parents.find(bnk_path);
    if (it != S.nested_bnk_parents.end()) {
        return normalized_asset_path(it->second);
    }
    std::filesystem::path p(bnk_path);
    return normalized_asset_path(p.parent_path().string());
}
