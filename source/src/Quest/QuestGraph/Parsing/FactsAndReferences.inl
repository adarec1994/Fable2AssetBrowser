ScriptFacts build_facts(const std::vector<std::string>& lines) {
    ScriptFacts facts;
    const std::regex assignment_re(
        R"assign(^\s*(?:local\s+)?([A-Za-z_][A-Za-z0-9_\.]*)\s*=\s*(.+)$)assign");
    for (std::size_t i = 0; i < lines.size(); ++i) {
        auto [statement, last] = gather_statement(lines, i, lines.size());
        i = last;
        std::smatch match;
        if (!std::regex_search(statement, match, assignment_re)) continue;
        const std::string variable = match[1].str();
        const std::string expression = match[2].str();
        const std::vector<std::string> strings = quoted_strings(expression);
        if (!strings.empty() &&
            expression.find("GetEntityWithName") == std::string::npos &&
            expression.find("FindAllOfCreatureType") == std::string::npos) {
            facts.strings[variable] = strings.front();
        }
        if ((expression.find("GetEntityWithName") != std::string::npos ||
             expression.find("FindAllOfCreatureType") != std::string::npos) &&
            !strings.empty()) {
            facts.entities[variable] = strings.front();
        }
        if (const auto coordinate = parse_coordinate(expression)) {
            facts.coordinates[variable] = *coordinate;
        }
        if (const auto number = parse_number(trim(expression))) {
            facts.numbers[variable] = *number;
        }
    }
    return facts;
}

std::optional<double> resolve_number(std::string expression,
                                     const ScriptFacts& facts) {
    expression = trim(std::move(expression));
    if (const auto direct = parse_number(expression)) return direct;
    auto found = facts.numbers.find(expression);
    if (found != facts.numbers.end()) return found->second;
    constexpr const char* parent_prefix = "self.ParentQuest.";
    if (expression.rfind(parent_prefix, 0) == 0) {
        const std::string owner_value =
            "self." + expression.substr(std::char_traits<char>::length(
                          parent_prefix));
        found = facts.numbers.find(owner_value);
        if (found != facts.numbers.end()) return found->second;
    }
    return std::nullopt;
}

std::string resolve_string(std::string expression, const ScriptFacts& facts) {
    expression = trim(std::move(expression));
    const std::vector<std::string> strings = quoted_strings(expression);
    if (!strings.empty()) return strings.front();
    auto it = facts.strings.find(expression);
    if (it != facts.strings.end()) return it->second;
    const std::size_t comma = expression.find(',');
    if (comma != std::string::npos) {
        it = facts.strings.find(trim(expression.substr(0, comma)));
        if (it != facts.strings.end()) return it->second;
    }
    return {};
}

std::string entity_name(std::string expression, const ScriptFacts& facts,
                        const std::string& attached_entity) {
    expression = trim(std::move(expression));
    if (expression.empty()) return attached_entity.empty() ? "Entity" : attached_entity;
    if (expression.find("QuestManager.HeroEntity") != std::string::npos ||
        expression == "GetPlayerHero()") return "Hero";
    if (expression == "self.Entity" && !attached_entity.empty()) {
        return humanize(attached_entity);
    }
    auto it = facts.entities.find(expression);
    if (it != facts.entities.end()) return humanize(it->second);
    const std::size_t get_name = expression.find(":GetName(");
    if (get_name != std::string::npos) {
        const std::string receiver = trim(expression.substr(0, get_name));
        it = facts.entities.find(receiver);
        if (it != facts.entities.end()) return humanize(it->second);
        if (receiver == "self.Entity" && !attached_entity.empty()) {
            return humanize(attached_entity);
        }
    }
    const std::vector<std::string> strings = quoted_strings(expression);
    if (!strings.empty()) return humanize(strings.front());
    const std::size_t comma = expression.find(',');
    if (comma != std::string::npos) expression.resize(comma);
    return humanize(expression);
}

std::optional<Coordinate> coordinate_for(const std::string& statement,
                                         const ScriptFacts& facts) {
    if (const auto direct = parse_coordinate(statement)) return direct;
    for (const auto& pair : facts.coordinates) {
        if (statement.find(pair.first) != std::string::npos) return pair.second;
    }
    return std::nullopt;
}

std::vector<std::string> tokens(const std::string& text) {
    static const std::unordered_set<std::string> ignored = {
        "text", "audio", "dialog", "dialogue", "voice", "voices",
        "wav", "xma", "quest", "quests", "scripts", "data"};
    std::vector<std::string> result;
    std::string token;
    for (unsigned char c : lower_ascii(text)) {
        if (std::isalnum(c)) {
            token.push_back(char(c));
        } else if (!token.empty()) {
            if (token.size() >= 2 && !ignored.count(token)) result.push_back(token);
            token.clear();
        }
    }
    if (token.size() >= 2 && !ignored.count(token)) result.push_back(token);
    return result;
}

std::vector<std::string> related_audio(const std::string& dialogue_id,
                                       const ReferenceCatalog& catalog) {
    const std::string exact_id = lower_ascii(dialogue_id);
    auto indexed = catalog.audio_by_dialogue.find(exact_id);
    if (indexed != catalog.audio_by_dialogue.end()) return indexed->second;
    if (!catalog.audio_by_dialogue.empty()) {
        std::vector<std::string> variants;
        const std::string prefix = exact_id + "_";
        for (const auto& entry : catalog.audio_by_dialogue) {
            if (entry.first.rfind(prefix, 0) != 0) continue;
            for (const std::string& path : entry.second) {
                append_unique(variants, path);
            }
        }
        std::sort(variants.begin(), variants.end());
        if (variants.size() > 6) variants.resize(6);
        return variants;
    }
    std::vector<std::string> exact;
    for (const std::string& asset : catalog.audio_assets) {
        std::string leaf = asset;
        const std::size_t slash = leaf.find_last_of("/\\");
        if (slash != std::string::npos) leaf.erase(0, slash + 1);
        const std::size_t dot = leaf.find_last_of('.');
        if (dot != std::string::npos) leaf.resize(dot);
        if (lower_ascii(leaf) == exact_id) append_unique(exact, asset);
    }
    if (!exact.empty()) return exact;

    const std::vector<std::string> wanted = tokens(dialogue_id);
    if (wanted.empty()) return {};
    std::vector<std::pair<int, std::string>> ranked;
    for (const std::string& asset : catalog.audio_assets) {
        const std::vector<std::string> available = tokens(asset);
        int matches = 0;
        int long_matches = 0;
        for (const std::string& want : wanted) {
            if (std::find(available.begin(), available.end(), want) !=
                available.end()) {
                ++matches;
                if (want.size() >= 5) ++long_matches;
            }
        }
        const int score = matches * 3 + long_matches * 2;
        if (score >= 6 && (matches >= 2 || long_matches >= 1)) {
            ranked.push_back({score, asset});
        }
    }
    std::sort(ranked.begin(), ranked.end(),
              [](const auto& a, const auto& b) {
                  if (a.first != b.first) return a.first > b.first;
                  return a.second < b.second;
              });
    std::vector<std::string> result;
    for (std::size_t i = 0; i < ranked.size() && i < 3; ++i) {
        result.push_back(ranked[i].second);
    }
    return result;
}

std::string localized_text(const std::string& id,
                           const ReferenceCatalog& catalog) {
    auto it = catalog.localized_text.find(id);
    if (it != catalog.localized_text.end()) return collapse_space(it->second);
    const std::string lower = lower_ascii(id);
    for (const auto& pair : catalog.localized_text) {
        if (lower_ascii(pair.first) == lower) return collapse_space(pair.second);
    }
    return {};
}

const CutsceneReference* cutscene_reference(
    const std::string& id, const ReferenceCatalog& catalog) {
    auto it = catalog.cutscenes.find(id);
    if (it != catalog.cutscenes.end()) return &it->second;
    const std::string lower = lower_ascii(id);
    for (const auto& pair : catalog.cutscenes) {
        if (lower_ascii(pair.first) == lower) return &pair.second;
    }
    return nullptr;
}

std::string choose_text_id(const std::vector<std::string>& args,
                           const ScriptFacts& facts,
                           const ReferenceCatalog& catalog) {
    std::string fallback;
    for (const std::string& arg : args) {
        const std::string value = resolve_string(arg, facts);
        if (value.empty()) continue;
        if (fallback.empty()) fallback = value;
        const std::string lower = lower_ascii(value);
        if (lower.rfind("text_", 0) == 0 ||
            lower.find("objective") != std::string::npos ||
            !localized_text(value, catalog).empty()) return value;
    }
    return fallback;
}

std::string event_description(const std::string& expression) {
    if (contains_ci(expression, "INTERACTED")) return "player interaction";
    if (contains_ci(expression, "OBJECT_DESTROYED")) return "the object being destroyed";
    if (contains_ci(expression, "KILLED")) return "the target being killed";
    if (contains_ci(expression, "HIT")) return "a hit";
    if (contains_ci(expression, "TRIGGER")) return "the trigger event";
    return humanize(expression);
}
