std::string sanitize_name(const std::string& raw)
{
    std::string out;
    out.reserve(raw.size());
    for (unsigned char c : raw) {
        if (std::isalnum(c)) out.push_back((char)std::tolower(c));
        else if (c == '_' || c == '-' || c == '.') out.push_back((char)c);
        else if (c == ' ') out.push_back('_');
    }
    while (!out.empty() && out.front() == '.') out.erase(out.begin());
    if (out.empty()) out = "asset";
    return out;
}
