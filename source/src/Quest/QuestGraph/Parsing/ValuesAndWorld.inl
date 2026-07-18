std::optional<Coordinate> parse_coordinate(const std::string& text) {
    static const std::regex vector_re(
        R"coord(CVector3\s*\(\s*([+-]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][+-]?[0-9]+)?)\s*,\s*([+-]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][+-]?[0-9]+)?)\s*,\s*([+-]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][+-]?[0-9]+)?)\s*\))coord");
    std::smatch match;
    if (!std::regex_search(text, match, vector_re)) return std::nullopt;
    try {
        return Coordinate{std::stod(match[1].str()), std::stod(match[2].str()),
                          std::stod(match[3].str())};
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<Coordinate> parse_coordinate_after(
    const std::string& text, const std::string& field) {
    const std::size_t position = lower_ascii(text).find(lower_ascii(field));
    if (position == std::string::npos) return std::nullopt;
    return parse_coordinate(text.substr(position + field.size()));
}

std::optional<double> parse_number_after(
    const std::string& text, const std::string& field) {
    const std::size_t position = lower_ascii(text).find(lower_ascii(field));
    if (position == std::string::npos) return std::nullopt;
    const std::string tail = text.substr(position + field.size());
    static const std::regex number_re(
        R"number(=\s*([+-]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][+-]?[0-9]+)?))number");
    std::smatch match;
    if (!std::regex_search(tail, match, number_re)) return std::nullopt;
    try {
        return std::stod(match[1].str());
    } catch (...) {
        return std::nullopt;
    }
}

std::string format_number(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(2) << value;
    std::string text = out.str();
    while (text.size() > 1 && text.back() == '0') text.pop_back();
    if (!text.empty() && text.back() == '.') text.pop_back();
    return text;
}

std::string format_coordinate(const Coordinate& value) {
    return "X " + format_number(value.x) + ", Y " + format_number(value.y) +
           ", Z " + format_number(value.z);
}

std::optional<double> parse_number(std::string expression) {
    expression = trim(std::move(expression));
    static const std::regex number_re(
        R"number(^[+-]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][+-]?[0-9]+)?$)number");
    if (!std::regex_match(expression, number_re)) return std::nullopt;
    try {
        return std::stod(expression);
    } catch (...) {
        return std::nullopt;
    }
}

void set_world_position(QuestEvent& event, const Coordinate& coordinate,
                        std::string level = {}, std::string marker = {}) {
    event.world.x = coordinate.x;
    event.world.y = coordinate.y;
    event.world.z = coordinate.z;
    event.world.resolved = true;
    event.world.level = std::move(level);
    event.world.marker = std::move(marker);
}

const std::vector<WorldEntityPlacement>* world_placements(
    const ReferenceCatalog& catalog, const std::string& marker) {
    const auto found = catalog.world_entities.find(lower_ascii(marker));
    if (found != catalog.world_entities.end()) return &found->second;
    for (const auto& entry : catalog.world_entities) {
        if (lower_ascii(entry.first) == lower_ascii(marker)) {
            return &entry.second;
        }
    }
    return nullptr;
}

void resolve_world_marker(NarrativeAction& action,
                          const ReferenceCatalog& catalog,
                          const std::string& marker) {
    action.event.world.marker = marker;
    const std::vector<WorldEntityPlacement>* placements =
        world_placements(catalog, marker);
    if (!placements || placements->empty()) {
        action.extra.push_back("Marker ID: " + marker);
        action.extra.push_back(
            "World placement: coordinates not indexed for this marker");
        return;
    }
    const WorldEntityPlacement& placement = placements->front();
    set_world_position(action.event,
                       Coordinate{placement.x, placement.y, placement.z},
                       placement.level, marker);
    action.extra.push_back("Marker ID: " + marker);
    if (!placement.level.empty()) {
        action.extra.push_back("Level: " + humanize(placement.level));
    }
    action.extra.push_back("Position: " + format_coordinate(
        Coordinate{placement.x, placement.y, placement.z}));
    if (placements->size() > 1) {
        action.event.properties.push_back(
            std::to_string(placements->size()) +
            " matching world placements; first match shown");
    }
}

void append_unique(std::vector<std::string>& values, std::string value) {
    value = trim(std::move(value));
    if (value.empty()) return;
    const std::string key = lower_ascii(value);
    for (const std::string& existing : values) {
        if (lower_ascii(existing) == key) return;
    }
    values.push_back(std::move(value));
}
