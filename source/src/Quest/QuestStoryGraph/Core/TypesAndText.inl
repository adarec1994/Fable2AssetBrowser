enum class BeatKind {
    Dialogue,
    ActorAction,
    Camera,
    Objective,
    Decision,
    Interaction,
    Inventory,
    Timer,
    WorldState,
    Task,
    Reward,
    Ending,
};

struct Beat {
    BeatKind kind = BeatKind::Task;
    bool source_boundary = false;
    std::string title;
    std::string subtitle;
    std::vector<std::string> details;
    std::vector<std::string> metadata;
    std::vector<QuestEvent> events;
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

bool starts_ci(const std::string& text, const std::string& prefix) {
    if (text.size() < prefix.size()) return false;
    return lower_ascii(text.substr(0, prefix.size())) == lower_ascii(prefix);
}

bool contains_ci(const std::string& text, const std::string& value) {
    return lower_ascii(text).find(lower_ascii(value)) != std::string::npos;
}

std::string strip_number(std::string line) {
    line = trim(std::move(line));
    std::size_t position = 0;
    while (position < line.size() && std::isdigit(
               static_cast<unsigned char>(line[position]))) ++position;
    if (position > 0 && position + 1 < line.size() &&
        line[position] == '.' && line[position + 1] == ' ') {
        line.erase(0, position + 2);
    }
    return trim(std::move(line));
}

std::optional<std::size_t> numbered_action_index(std::string line) {
    line = trim(std::move(line));
    std::size_t position = 0;
    while (position < line.size() && std::isdigit(
               static_cast<unsigned char>(line[position]))) ++position;
    if (position == 0 || position + 1 >= line.size() ||
        line[position] != '.' || line[position + 1] != ' ') {
        return std::nullopt;
    }
    try {
        const std::size_t one_based = std::stoul(line.substr(0, position));
        if (one_based == 0) return std::nullopt;
        return one_based - 1;
    } catch (...) {
        return std::nullopt;
    }
}

std::string clean_names(std::string value) {
    std::replace(value.begin(), value.end(), '_', ' ');
    static const std::regex quest_prefix(
        R"(\bQ[A-Za-z]*[0-9]+[ \-]+)", std::regex::icase);
    value = std::regex_replace(value, quest_prefix, "");
    static const std::regex player_hero(R"(\bPlayer Hero\b)",
                                        std::regex::icase);
    value = std::regex_replace(value, player_hero, "Hero");
    if (value.size() > 4 && lower_ascii(value.substr(value.size() - 4)) ==
                                ".bik") {
        value.resize(value.size() - 4);
    }
    value = trim(std::move(value));
    std::string spaced;
    spaced.reserve(value.size() + 8);
    for (std::size_t i = 0; i < value.size(); ++i) {
        const unsigned char current = static_cast<unsigned char>(value[i]);
        const unsigned char previous = i == 0
            ? 0 : static_cast<unsigned char>(value[i - 1]);
        if (i > 0 && value[i - 1] != ' ' &&
            ((std::isupper(current) && std::islower(previous)) ||
             (std::isdigit(current) && std::isalpha(previous)))) {
            spaced.push_back(' ');
        }
        spaced.push_back(value[i]);
    }
    return trim(std::move(spaced));
}

std::string clean_participants(std::string value) {
    std::string result;
    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t comma = value.find(',', start);
        const std::string part = clean_names(value.substr(
            start, comma == std::string::npos ? std::string::npos
                                               : comma - start));
        if (!part.empty()) {
            if (!result.empty()) result += " / ";
            result += part;
        }
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return result;
}

bool meaningful_condition(const std::string& line) {
    const std::string lower = lower_ascii(line);
    static const char* terms[] = {
        "trigger", "within", "enters", "inside", "killed", "dead",
        "destroy", "interact", "collected", "enough", "money",
        "target", "objective", "has ", "door", "completed",
    };
    for (const char* term : terms) {
        if (lower.find(term) != std::string::npos) return true;
    }
    return false;
}

std::string decision_title(std::string line) {
    if (starts_ci(line, "Wait for trigger ")) {
        line.erase(0, std::string("Wait for trigger ").size());
        const std::string suffix = " to fire";
        if (line.size() >= suffix.size() &&
            lower_ascii(line.substr(line.size() - suffix.size())) == suffix) {
            line.resize(line.size() - suffix.size());
        }
        return "Has " + clean_names(line) + " been triggered?";
    }
    if (starts_ci(line, "Wait until ")) {
        line.erase(0, std::string("Wait until ").size());
    }
    const std::string becomes_true = " becomes true";
    if (line.size() >= becomes_true.size() &&
        lower_ascii(line.substr(line.size() - becomes_true.size())) ==
            becomes_true) {
        line.resize(line.size() - becomes_true.size());
    }
    line = clean_names(line);
    const std::string inside_call =
        "Trigger.Is Specific Trigger Entity Inside Trigger Volume(";
    if (starts_ci(line, inside_call)) {
        const std::size_t close = line.find(')');
        const std::string args = line.substr(
            inside_call.size(), close == std::string::npos
                                    ? std::string::npos
                                    : close - inside_call.size());
        const std::size_t comma = args.find(',');
        if (comma != std::string::npos) {
            const std::string trigger = clean_names(args.substr(0, comma));
            const std::string actor = clean_names(args.substr(comma + 1));
            return "Is " + actor + " inside " + trigger + "?";
        }
    }
    if (starts_ci(line, "the trigger is entered")) {
        return "Has the trigger been entered?";
    }
    if (contains_ci(line, " is within ") || contains_ci(line, " enters ") ||
        contains_ci(line, " is dead") || contains_ci(line, " is killed")) {
        if (!line.empty()) line[0] = char(std::toupper(
            static_cast<unsigned char>(line[0])));
        return "Is " + line + "?";
    }
    return "Has " + line + " happened?";
}
