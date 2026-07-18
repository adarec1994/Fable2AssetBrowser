std::string PrettifyTagLabel(std::string tag)
{
    for (const char* pfx : { "INV_ITEM_", "OBJECT_", "TEXT_" }) {
        const size_t n = std::strlen(pfx);
        if (tag.size() > n && tag.compare(0, n, pfx) == 0) {
            tag = tag.substr(n);
            break;
        }
    }
    if (tag.size() > 5 && tag.compare(tag.size() - 5, 5, "_NAME") == 0) {
        tag.resize(tag.size() - 5);
    }
    const bool has_lower = std::any_of(
        tag.begin(), tag.end(),
        [](unsigned char c) { return std::islower(c) != 0; });
    if (tag.find('_') != std::string::npos || !has_lower) {
        bool word_start = true;
        for (auto& c : tag) {
            if (c == '_') {
                c = ' ';
                word_start = true;
            } else {
                c = word_start ? char(std::toupper((unsigned char)c))
                               : char(std::tolower((unsigned char)c));
                word_start = false;
            }
        }
    }
    return tag;
}
