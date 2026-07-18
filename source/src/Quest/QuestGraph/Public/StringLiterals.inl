std::vector<std::string> FindLuaStringLiterals(const std::string& text) {
    std::vector<std::string> result;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '"' && text[i] != '\'') continue;
        const char quote = text[i++];
        std::string value;
        bool escaped = false;
        for (; i < text.size(); ++i) {
            const char c = text[i];
            if (escaped) {
                if (c == 'n') value.push_back(' ');
                else value.push_back(c);
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == quote) {
                break;
            } else {
                value.push_back(c);
            }
        }
        result.push_back(std::move(value));
    }
    return result;
}
