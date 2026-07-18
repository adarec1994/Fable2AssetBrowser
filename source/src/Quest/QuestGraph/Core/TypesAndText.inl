struct ThreadDecl {
    std::string class_name;
    bool entity = false;
};

struct FunctionBlock {
    std::string class_name;
    std::string method;
    std::size_t begin = 0;
    std::size_t end = 0;
};

struct StateBranch {
    int value = 0;
    std::size_t begin = 0;
    std::size_t end = 0;
    int node_id = 0;
};

struct Coordinate {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct ScriptFacts {
    std::unordered_map<std::string, std::string> strings;
    std::unordered_map<std::string, std::string> entities;
    std::unordered_map<std::string, Coordinate> coordinates;
    std::unordered_map<std::string, double> numbers;
};

struct NarrativeAction {
    std::string summary;
    std::vector<std::string> extra;
    QuestEvent event;
    std::string starts_thread;
    std::string entity_instance;
    std::optional<int> transition;
    std::size_t dialogue_lines = 0;
    std::size_t audio_matches = 0;
    bool terminal = false;
};

struct Narrative {
    std::vector<NarrativeAction> actions;



    std::vector<QuestEvent> supplemental_events;
    std::vector<int> transitions;
    std::vector<std::string> entities;
    std::vector<std::string> locations;
    std::vector<std::string> markers;
    std::vector<std::string> coordinates;
    std::vector<std::string> self_calls;
    bool terminal = false;
};

std::string trim(std::string value) {
    const auto not_space = [](unsigned char c) { return !std::isspace(c); };
    value.erase(value.begin(),
                std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(),
                value.end());
    return value;
}

std::string lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    return value;
}

bool contains_ci(const std::string& text, const std::string& needle) {
    return lower_ascii(text).find(lower_ascii(needle)) != std::string::npos;
}

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> result;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        result.push_back(std::move(line));
    }
    return result;
}

std::string collapse_space(std::string text) {
    std::string out;
    bool previous_space = false;
    for (unsigned char c : text) {
        const bool space = std::isspace(c) != 0;
        if (space) {
            if (!out.empty() && !previous_space) out.push_back(' ');
        } else {
            out.push_back(char(c));
        }
        previous_space = space;
    }
    return trim(out);
}

std::string humanize(std::string value) {
    value = trim(value);
    if (value.rfind("self.", 0) == 0) value.erase(0, 5);
    if (value.rfind("QuestManager.", 0) == 0) value.erase(0, 13);
    const std::size_t slash = value.find_last_of("/\\");
    if (slash != std::string::npos) value.erase(0, slash + 1);
    const std::size_t dot = value.find_last_of('.');
    if (dot != std::string::npos && dot + 1 < value.size()) {
        const std::string ext = lower_ascii(value.substr(dot + 1));
        if (ext == "wav" || ext == "xma" || ext == "engine_level" ||
            ext == "lua") {
            value.resize(dot);
        }
    }

    std::string out;
    for (std::size_t i = 0; i < value.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(value[i]);
        if (c == '_' || c == '-') {
            if (!out.empty() && out.back() != ' ') out.push_back(' ');
            continue;
        }
        if (i > 0 && std::isupper(c) &&
            std::islower(static_cast<unsigned char>(value[i - 1])) &&
            !out.empty() && out.back() != ' ') {
            out.push_back(' ');
        }
        out.push_back(char(c));
    }
    return collapse_space(out);
}

std::string shorten(std::string text, std::size_t max_length) {
    text = collapse_space(std::move(text));
    if (text.size() <= max_length) return text;
    text.resize(max_length > 3 ? max_length - 3 : max_length);
    return trim(text) + "...";
}

std::vector<std::string> quoted_strings(const std::string& text) {
    return FindLuaStringLiterals(text);
}

std::vector<std::string> split_arguments(const std::string& body) {
    std::vector<std::string> result;
    std::size_t start = 0;
    int depth = 0;
    char quote = 0;
    bool escaped = false;
    for (std::size_t i = 0; i < body.size(); ++i) {
        const char c = body[i];
        if (quote) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == quote) quote = 0;
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '(' || c == '{' || c == '[') {
            ++depth;
        } else if (c == ')' || c == '}' || c == ']') {
            --depth;
        } else if (c == ',' && depth == 0) {
            result.push_back(trim(body.substr(start, i - start)));
            start = i + 1;
        }
    }
    if (start < body.size()) result.push_back(trim(body.substr(start)));
    return result;
}

std::vector<std::string> call_arguments(const std::string& statement,
                                        const std::string& call_name) {
    const std::size_t call = statement.find(call_name);
    if (call == std::string::npos) return {};
    const std::size_t open = statement.find('(', call + call_name.size());
    if (open == std::string::npos) return {};
    int depth = 1;
    char quote = 0;
    bool escaped = false;
    for (std::size_t i = open + 1; i < statement.size(); ++i) {
        const char c = statement[i];
        if (quote) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == quote) quote = 0;
            continue;
        }
        if (c == '"' || c == '\'') quote = c;
        else if (c == '(') ++depth;
        else if (c == ')' && --depth == 0) {
            return split_arguments(statement.substr(open + 1,
                                                     i - open - 1));
        }
    }
    return split_arguments(statement.substr(open + 1));
}

bool contains_call(const std::string& statement, const std::string& call_name) {
    std::size_t position = 0;
    while ((position = statement.find(call_name, position)) != std::string::npos) {
        const bool boundary = position == 0 ||
            (!std::isalnum(static_cast<unsigned char>(statement[position - 1])) &&
             statement[position - 1] != '_');
        std::size_t after = position + call_name.size();
        while (after < statement.size() &&
               std::isspace(static_cast<unsigned char>(statement[after]))) ++after;
        if (boundary && after < statement.size() && statement[after] == '(') {
            return true;
        }
        position += call_name.size();
    }
    return false;
}

std::pair<std::string, std::size_t> gather_statement(
    const std::vector<std::string>& lines, std::size_t begin,
    std::size_t end) {
    std::string statement = trim(lines[begin]);
    int depth = 0;
    char quote = 0;
    bool escaped = false;
    auto consume = [&](const std::string& part) {
        for (char c : part) {
            if (quote) {
                if (escaped) escaped = false;
                else if (c == '\\') escaped = true;
                else if (c == quote) quote = 0;
                continue;
            }
            if (c == '"' || c == '\'') quote = c;
            else if (c == '(' || c == '{' || c == '[') ++depth;
            else if (c == ')' || c == '}' || c == ']') --depth;
        }
    };
    consume(statement);
    std::size_t last = begin;
    while ((depth > 0 || quote) && last + 1 < end && last - begin < 24) {
        ++last;
        const std::string next = trim(lines[last]);
        if (!next.empty()) statement += " " + next;
        consume(next);
    }
    return {statement, last};
}
