#include "QuestGraph.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace Quest {
namespace {

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

std::string condition_description(std::string expression,
                                  const ScriptFacts& facts,
                                  const std::string& attached_entity) {
    expression = trim(std::move(expression));
    if (expression.empty()) return "the scripted condition is met";
    while (!expression.empty() && expression.back() == ')') {
        const std::size_t opens = std::count(expression.begin(), expression.end(), '(');
        const std::size_t closes = std::count(expression.begin(), expression.end(), ')');
        if (closes <= opens) break;
        expression.pop_back();
        expression = trim(std::move(expression));
    }
    if (expression.rfind("not ", 0) == 0) {
        return humanize(expression.substr(4)) + " becomes false";
    }
    if (expression == "true") return "the next scripted update begins";
    if (expression.find("IsAvailableToSayLine") != std::string::npos) {
        const auto args = call_arguments(expression, "IsAvailableToSayLine");
        const std::string speaker = args.empty()
            ? (attached_entity.empty() ? "the NPC"
                                       : humanize(attached_entity))
            : entity_name(args.front(), facts, attached_entity);
        return speaker + " is ready to speak";
    }
    if (expression.find("IsMessageSentTo") != std::string::npos) {
        const auto args = call_arguments(expression, "IsMessageSentTo");
        const std::string event = args.empty() ? "the required event"
                                               : event_description(args[0]);
        const std::string target = args.size() >= 2
            ? entity_name(args[1], facts, attached_entity)
            : humanize(attached_entity);
        return event + (target.empty() ? std::string(" occurs")
                                       : " involving " + target);
    }
    if (expression.find("IsTriggerEntityInsideTrigger") != std::string::npos) {
        const std::string call = expression.find("IsTriggerEntityInsideTriggerVolume") !=
                                         std::string::npos
            ? "IsTriggerEntityInsideTriggerVolume"
            : "IsTriggerEntityInsideTrigger";
        const auto args = call_arguments(expression, call);
        if (args.size() >= 2) {
            return entity_name(args[1], facts, attached_entity) + " enters " +
                   entity_name(args[0], facts, attached_entity);
        }
    }
    if (expression.find("==") != std::string::npos) {
        const std::size_t op = expression.find("==");
        return humanize(expression.substr(0, op)) + " equals " +
               humanize(expression.substr(op + 2));
    }
    if (expression.find("~=") != std::string::npos) {
        const std::size_t op = expression.find("~=");
        return humanize(expression.substr(0, op)) + " no longer equals " +
               humanize(expression.substr(op + 2));
    }
    return humanize(expression) + " becomes true";
}

NarrativeAction describe_statement(const std::string& statement,
                                   const std::string& attached_entity,
                                   const ScriptFacts& facts,
                                   const ReferenceCatalog& catalog) {
    NarrativeAction action;
    action.event.source_statement = statement;
    std::smatch transition;
    static const std::regex transition_re(
        R"(self\.CurrentState\s*=\s*(-?[0-9]+))");
    if (std::regex_search(statement, transition, transition_re)) {
        action.transition = std::stoi(transition[1].str());
    } else if (contains_ci(statement,
                           "self.CurrentState = self.CurrentState + 1")) {
        action.transition = -2147483647;
    }

    const std::vector<std::string> dialogue_calls = {
        "SaySimLine", "SayLine", "TalkToEntity", "PlaySpeech",
        "PostGuildSealMessage", "ShowToasterBoxWithDialogue",
        "DisplayInfoBox", "DisplayMessageBox"};
    for (const std::string& call : dialogue_calls) {
        if (!contains_call(statement, call)) continue;
        const std::vector<std::string> args = call_arguments(statement, call);
        const std::string id = choose_text_id(args, facts, catalog);
        const std::string id_lower = lower_ascii(id);
        const bool display_call = call.rfind("Display", 0) == 0;
        if (display_call && id_lower.rfind("text_", 0) != 0 &&
            localized_text(id, catalog).empty()) {
            continue;
        }
        std::string speaker = "Game UI";
        if (call == "PostGuildSealMessage") speaker = "Guild Seal";
        else if (call.find("Display") == std::string::npos && !args.empty()) {
            speaker = entity_name(args.front(), facts, attached_entity);
        } else if (!attached_entity.empty() && call.find("Display") == std::string::npos) {
            speaker = humanize(attached_entity);
        }
        const std::string text = localized_text(id, catalog);
        if (!text.empty()) {
            action.summary = speaker + " says: \"" + shorten(text, 180) + "\"";
        } else if (!id.empty()) {
            action.summary = speaker + " speaks dialogue " + id;
        } else {
            action.summary = speaker + " speaks";
        }
        if (!id.empty()) action.extra.push_back("Dialogue ID: " + id);
        action.event.kind = QuestEventKind::Dialogue;
        action.event.actor = speaker;
        action.event.dialogue_id = id;
        for (const std::string& audio : related_audio(id, catalog)) {
            action.extra.push_back("Related audio: " + audio);
            ++action.audio_matches;
        }
        action.dialogue_lines = 1;
        return action;
    }

    if (statement.find("StartNewEntityThread") != std::string::npos) {
        const auto args = call_arguments(statement, "StartNewEntityThread");
        if (!args.empty()) action.entity_instance = resolve_string(args[0], facts);
        if (action.entity_instance.empty() && !args.empty()) {
            action.entity_instance = entity_name(args[0], facts, attached_entity);
        }
        if (args.size() >= 2) action.starts_thread = trim(args[1]);
        const std::string actor = action.entity_instance.empty()
            ? humanize(action.starts_thread)
            : humanize(action.entity_instance);
        action.summary = "Start " + actor + "'s scripted behaviour";
        action.event.kind = QuestEventKind::ScriptCall;
        action.event.actor = actor;
        action.event.target = action.starts_thread;
        return action;
    }

    if (statement.find("QuestTracker.SetAsCompleted") != std::string::npos ||
        statement.find("MissionSucceeded = true") != std::string::npos) {
        action.summary = "Complete the quest successfully";
        action.event.kind = QuestEventKind::QuestComplete;
        action.terminal = true;
        return action;
    }
    if (statement.find("MissionFailed = true") != std::string::npos ||
        statement.find("QuestTracker.SetAsFailed") != std::string::npos) {
        action.summary = "Fail the quest";
        action.event.kind = QuestEventKind::QuestFail;
        action.terminal = true;
        return action;
    }
    if (statement.find("QuestTracker.SetAsActive") != std::string::npos) {
        action.summary = "Mark the quest as active";
        action.event.kind = QuestEventKind::QuestStart;
        return action;
    }
    if (statement.find("QuestTracker.SetAsPrimary") != std::string::npos) {
        action.summary = "Make this the primary quest";
        return action;
    }

    const std::vector<std::string> objective_calls = {
        "UpdateObjectiveTag", "SetObjectiveTag", "SetObjectiveLevelAndExit",
        "SetObjectiveLevel",
        "SetObjectiveEntity", "SetObjectiveBreadcrumbRadius",
        "RemoveObjectiveTag", "SetObjectiveAsCompleted"};
    for (const std::string& call : objective_calls) {
        if (statement.find(call) == std::string::npos) continue;
        const auto args = call_arguments(statement, call);
        const std::string id = choose_text_id(args, facts, catalog);
        const std::string text = localized_text(id, catalog);
        if (call == "SetObjectiveEntity") {
            std::size_t target_index = args.empty() ? 0 : args.size() - 1;
            if (args.size() >= 2 &&
                (contains_ci(args.back(), "true") ||
                 contains_ci(args.back(), "false"))) {
                target_index = args.size() - 2;
            }
            const std::string target = args.empty()
                ? "the target entity"
                : entity_name(args[target_index], facts, attached_entity);
            action.summary = "Point the objective marker at " + target;
            action.event.kind = QuestEventKind::ObjectiveTarget;
            action.event.target = target;
            const std::string marker = args.empty()
                ? std::string() : resolve_string(args[target_index], facts);
            if (!marker.empty()) resolve_world_marker(action, catalog, marker);
        } else if (call == "SetObjectiveLevel" ||
                   call == "SetObjectiveLevelAndExit") {
            action.summary = "Set the objective destination to " +
                             humanize(id.empty() ? "the target level" : id);
            action.event.kind = QuestEventKind::ObjectiveTarget;
            action.event.world.level = id;
        } else if (call == "RemoveObjectiveTag") {
            action.summary = "Remove objective: " +
                             (text.empty() ? humanize(id) : text);
            action.event.kind = QuestEventKind::ObjectiveRemove;
        } else if (call == "SetObjectiveAsCompleted") {
            action.summary = "Mark the current objective complete";
            action.event.kind = QuestEventKind::ObjectiveComplete;
        } else {
            action.summary = "Set objective: " +
                             (text.empty() ? humanize(id) : text);
            action.event.kind = QuestEventKind::ObjectiveSet;
        }
        action.event.objective = id;
        if (!id.empty()) action.extra.push_back("Objective ID: " + id);
        return action;
    }

    if (contains_call(statement, "PlayCutscene") ||
        contains_call(statement, "InteractiveCutscene")) {
        const auto strings = quoted_strings(statement);
        const std::string name = strings.empty() ? std::string() : strings.front();
        action.summary = name.empty() ? "Play the configured cutscene"
                                      : "Play cutscene " + humanize(name);
        action.event.kind = QuestEventKind::Cutscene;
        action.event.cutscene_id = name;
        if (!name.empty()) action.extra.push_back("Cutscene ID: " + name);
        if (const CutsceneReference* reference =
                cutscene_reference(name, catalog)) {
            if (!reference->speakers.empty()) {
                std::string speakers;
                for (const std::string& speaker : reference->speakers) {
                    if (!speakers.empty()) speakers += ", ";
                    speakers += humanize(speaker);
                }
                action.extra.push_back("Participants/NPCs: " + speakers);
            }
            std::size_t spoken_lines = reference->dialogue_tags.size();
            if (!reference->timeline.empty()) {
                spoken_lines = 0;
                for (const CutsceneTimelineEntry& entry :
                     reference->timeline) {
                    if (entry.kind == CutsceneTimelineKind::Dialogue) {
                        ++spoken_lines;
                    }
                }
            }
            action.dialogue_lines = spoken_lines;
            if (spoken_lines > 0) {
                action.summary += " (" +
                    std::to_string(spoken_lines) +
                    (spoken_lines == 1
                         ? " spoken line)" : " spoken lines)");
            }

            auto append_dialogue = [&](const std::string& tag,
                                       const std::string& known_speaker) {
                const std::string text = localized_text(tag, catalog);
                std::string speaker = humanize(known_speaker);
                if (speaker.empty()) {
                    for (const CutsceneDialogueLine& line :
                         reference->dialogue_lines) {
                        if (lower_ascii(line.text_tag) == lower_ascii(tag)) {
                            speaker = humanize(line.speaker);
                            break;
                        }
                    }
                }
                action.extra.push_back(
                    "Dialogue [" + tag + "]" +
                    (speaker.empty() ? std::string() : " " + speaker) +
                    ": " + shorten(text.empty() ? humanize(tag) : text, 220));
                for (const std::string& audio : related_audio(tag, catalog)) {
                    action.extra.push_back("Related audio: " + audio);
                    ++action.audio_matches;
                }
            };

            if (!reference->timeline.empty()) {
                for (const CutsceneTimelineEntry& entry :
                     reference->timeline) {
                    if (entry.kind == CutsceneTimelineKind::Dialogue) {
                        append_dialogue(entry.text_tag, entry.speaker);
                        continue;
                    }
                    action.extra.push_back(
                        "Actor action: " + entry.description);
                    for (const std::string& detail : entry.details) {
                        action.extra.push_back("Actor detail: " + detail);
                    }
                    for (const std::string& metadata : entry.metadata) {
                        action.extra.push_back(metadata);
                    }
                }
            } else {
                for (const std::string& tag : reference->dialogue_tags) {
                    append_dialogue(tag, {});
                }
            }
        }
        return action;
    }

    if (contains_call(statement, "SetLookAtCamera")) {
        const bool childhood_castle_view =
            contains_ci(attached_entity, "QC010 Rose") &&
            statement.find("dof_focus_position") != std::string::npos;
        action.summary = childhood_castle_view
            ? "Camera event: Sparrow is prompted to look at Castle Fairfax"
            : "Camera event: The camera focuses on a story point";
        action.event.kind = QuestEventKind::Camera;
        if (const auto source = parse_coordinate_after(
                statement, "source_position")) {
            action.extra.push_back(
                "Camera position: " + format_coordinate(*source));
        }
        if (const auto target = parse_coordinate_after(
                statement, "target_position")) {
            action.extra.push_back(
                "Camera focus: " + format_coordinate(*target));
            set_world_position(action.event, *target);
        }
        if (const auto fov = parse_number_after(statement, "fov")) {
            action.extra.push_back(
                "Field of view: " + format_number(*fov) + " degrees");
        }
        if (childhood_castle_view) {
            action.extra.push_back(
                "The look-at prompt lets the player focus on the castle "
                "before Rose continues speaking.");
        }
        return action;
    }
    if (statement.find("GUI.PlayMovie") != std::string::npos ||
        statement.find("GUI.PlayLocalisedMovie") != std::string::npos) {
        const auto strings = quoted_strings(statement);
        const std::string movie = strings.empty() ? "movie" : strings.front();
        action.summary = "Play movie " + humanize(movie);
        action.event.kind = QuestEventKind::Cutscene;
        action.event.cutscene_id = movie;
        action.extra.push_back("Movie asset: " + movie);
        return action;
    }

    const std::vector<std::string> level_calls = {
        "LoadAndWaitForLevel", "Debug.LoadLevel", "TeleportToLevel"};
    for (const std::string& call : level_calls) {
        if (statement.find(call) == std::string::npos) continue;
        const auto args = call_arguments(statement, call);
        std::vector<std::string> places;
        for (const std::string& arg : args) {
            const std::string place = resolve_string(arg, facts);
            if (!place.empty()) append_unique(places, humanize(place));
        }
        std::string destination;
        for (const std::string& place : places) {
            if (!destination.empty()) destination += " / ";
            destination += place;
        }
        action.summary = "Travel to " +
                         (destination.empty() ? std::string("the next area")
                                              : destination);
        action.event.kind = QuestEventKind::Travel;
        action.event.world.level = destination;
        return action;
    }

    if (statement.find("Layers.ActivateLayer") != std::string::npos ||
        statement.find("Layers.DeactivateLayer") != std::string::npos) {
        const bool activate = statement.find("ActivateLayer") != std::string::npos &&
                              statement.find("DeactivateLayer") == std::string::npos;
        const auto strings = quoted_strings(statement);
        const std::string layer = strings.empty() ? "quest layer" : strings.front();
        action.summary = std::string(activate ? "Enable " : "Disable ") +
                         humanize(layer) + " in the level";
        action.extra.push_back("Layer ID: " + layer);
        action.event.kind = QuestEventKind::LayerState;
        action.event.target = layer;
        action.event.properties.push_back(activate ? "enabled" : "disabled");
        return action;
    }

    if (statement.find("GUI.SetTimer") != std::string::npos ||
        statement.find("QuestManager.NewTimer") != std::string::npos) {
        const std::string call = statement.find("GUI.SetTimer") != std::string::npos
            ? "GUI.SetTimer" : "QuestManager.NewTimer";
        const auto args = call_arguments(statement, call);
        std::string timer_name;
        if (call == "GUI.SetTimer" && !args.empty()) {
            timer_name = resolve_string(args.front(), facts);
        }
        std::optional<double> duration;
        for (auto it = args.rbegin(); it != args.rend(); ++it) {
            duration = resolve_number(*it, facts);
            if (duration) break;
        }
        action.summary = "Start " +
            (timer_name.empty() ? std::string("quest timer")
                                : humanize(timer_name) + " timer") +
            (duration ? " for " + format_number(*duration) + " seconds"
                      : std::string());
        action.event.kind = QuestEventKind::TimerStart;
        action.event.target = timer_name;
        action.event.duration_seconds = duration;
        return action;
    }
    if (statement.find("GUI.RemoveTimer") != std::string::npos ||
        statement.find("GUI.StopTimer") != std::string::npos) {
        const std::string call = statement.find("GUI.RemoveTimer") != std::string::npos
            ? "GUI.RemoveTimer" : "GUI.StopTimer";
        const auto args = call_arguments(statement, call);
        const std::string timer_name = args.empty()
            ? std::string() : resolve_string(args.front(), facts);
        action.summary = "Stop " +
            (timer_name.empty() ? std::string("quest timer")
                                : humanize(timer_name) + " timer");
        action.event.kind = QuestEventKind::TimerStop;
        action.event.target = timer_name;
        return action;
    }
    if ((statement.find("GetTime()") != std::string::npos ||
         statement.find(":GetTime()") != std::string::npos) &&
        (statement.find("== 0") != std::string::npos ||
         statement.find("<= 0") != std::string::npos)) {
        action.summary = "Wait until the quest timer expires";
        action.event.kind = QuestEventKind::TimerWait;
        action.event.condition = "timer reaches zero";
        return action;
    }
    if (statement.find("DiggingSpot.SetAsDiggableWithoutDog") !=
            std::string::npos ||
        statement.find("SetAsDiggableWithoutDog") != std::string::npos) {
        const auto args = call_arguments(statement, "SetAsDiggableWithoutDog");
        const std::string target = args.empty()
            ? humanize(attached_entity)
            : entity_name(args.front(), facts, attached_entity);
        std::string marker = args.empty() ? std::string()
                                          : resolve_string(args.front(), facts);
        if (marker.empty() && !args.empty()) {
            for (const auto& entity : facts.entities) {
                if (trim(args.front()) == entity.first) {
                    marker = entity.second;
                    break;
                }
            }
        }
        action.summary = "Enable digging at " + target;
        action.event.kind = QuestEventKind::DigSpotEnable;
        action.event.target = target;
        if (!marker.empty()) resolve_world_marker(action, catalog, marker);
        return action;
    }
    if (statement.find("DiggingSpot.IsActive") != std::string::npos) {
        const auto args = call_arguments(statement, "DiggingSpot.IsActive");
        const std::string target = args.empty()
            ? humanize(attached_entity)
            : entity_name(args.front(), facts, attached_entity);
        std::string marker = args.empty() ? std::string()
                                          : resolve_string(args.front(), facts);
        if (marker.empty() && !args.empty()) {
            for (const auto& entity : facts.entities) {
                if (trim(args.front()) == entity.first) {
                    marker = entity.second;
                    break;
                }
            }
        }
        const bool complete = contains_ci(statement, "not ") ||
                              statement.find("== false") != std::string::npos;
        const bool branch_condition = trim(statement).rfind("if ", 0) == 0 ||
                                      trim(statement).rfind("elseif ", 0) == 0;
        action.summary = complete
            ? "Wait until the player finishes digging at " + target
            : branch_condition
                ? "Check whether the player is digging at " + target
                : "Wait until digging is active at " + target;
        action.event.kind = complete ? QuestEventKind::DigSpotComplete
                                     : QuestEventKind::Condition;
        action.event.target = target;
        action.event.condition = complete ? "digging completed"
                                          : "digging spot active";
        if (!marker.empty()) resolve_world_marker(action, catalog, marker);
        return action;
    }
    if (statement.find("WaitForTimeInSeconds") != std::string::npos) {
        const auto args = call_arguments(statement, "WaitForTimeInSeconds");
        action.summary = "Wait " +
                         (args.empty() ? std::string("for the scripted delay")
                                       : trim(args.front()) + " seconds");
        action.event.kind = QuestEventKind::TimerWait;
        if (!args.empty()) {
            action.event.duration_seconds = resolve_number(args.front(), facts);
        }
        return action;
    }
    if (statement.find("WaitForTriggerToFire") != std::string::npos) {
        const auto args = call_arguments(statement, "WaitForTriggerToFire");
        const std::string trigger = args.empty()
            ? "the trigger"
            : entity_name(args.front(), facts, attached_entity);
        action.summary = "Wait for trigger " + trigger + " to fire";
        action.event.kind = QuestEventKind::Condition;
        action.event.target = trigger;
        action.event.condition = "trigger fires";
        return action;
    }
    if (statement.find("IsDistanceBetweenThingsUnder") != std::string::npos) {
        const auto args = call_arguments(statement, "IsDistanceBetweenThingsUnder");
        if (args.size() >= 3) {
            action.summary = "Wait until " + entity_name(args[0], facts, attached_entity) +
                             " is within " + trim(args[2]) + " of " +
                             entity_name(args[1], facts, attached_entity);
        } else {
            action.summary = "Wait until the required entities are close enough";
        }
        action.event.kind = QuestEventKind::Condition;
        action.event.condition = action.summary;
        return action;
    }
    if (statement.find("IsMessageSentTo") != std::string::npos) {
        const auto args = call_arguments(statement, "IsMessageSentTo");
        const std::string event = args.empty() ? "the required event"
                                               : event_description(args[0]);
        const std::string target = args.size() >= 2
            ? entity_name(args[1], facts, attached_entity)
            : humanize(attached_entity);
        action.summary = "Wait for " + event +
                         (target.empty() ? std::string() : " involving " + target);
        action.event.kind = contains_ci(event, "interaction")
            ? QuestEventKind::Interaction : QuestEventKind::Condition;
        action.event.target = target;
        action.event.condition = event;
        return action;
    }
    if (statement.find("IsLevelLoaded") != std::string::npos) {
        const auto args = call_arguments(statement, "IsLevelLoaded");
        const std::string level = args.empty() ? "the destination"
                                               : resolve_string(args[0], facts);
        action.summary = "Wait for " + humanize(level) + " to finish loading";
        action.event.kind = QuestEventKind::Condition;
        action.event.world.level = level;
        action.event.condition = "level loaded";
        return action;
    }
    if (statement.find("IsTriggerEntityInsideTriggerVolume") != std::string::npos ||
        statement.find("IsTriggerEntityInsideTrigger") != std::string::npos) {
        const std::string call = statement.find("IsTriggerEntityInsideTriggerVolume") !=
                                         std::string::npos
            ? "IsTriggerEntityInsideTriggerVolume"
            : "IsTriggerEntityInsideTrigger";
        const auto args = call_arguments(statement, call);
        if (args.size() >= 2) {
            action.summary = "Wait until " + entity_name(args[1], facts, attached_entity) +
                             " enters " + entity_name(args[0], facts, attached_entity);
        } else {
            action.summary = "Wait until the trigger is entered";
        }
        action.event.kind = QuestEventKind::Condition;
        action.event.condition = action.summary;
        return action;
    }
    if (statement.find("self.Interacted") != std::string::npos ||
        statement.find(".Interacted") != std::string::npos) {
        std::string target = humanize(attached_entity);
        static const std::regex interacted_re(
            R"interact(([A-Za-z_][A-Za-z0-9_\.]*)\.Interacted)interact");
        std::smatch match;
        if (std::regex_search(statement, match, interacted_re)) {
            target = entity_name(match[1].str(), facts, attached_entity);
        }
        action.summary = "Wait for the player to interact with " + target;
        action.event.kind = QuestEventKind::Interaction;
        action.event.actor = "Hero";
        action.event.target = target;
        action.event.condition = "player interacts";
        return action;
    }
    if (statement.find(":WaitFor(") != std::string::npos) {
        std::string condition;
        static const std::regex return_re(
            R"wait(\breturn\s+(.+?)(?:\s+end\s*\)?\s*$|$))wait");
        std::smatch match;
        if (std::regex_search(statement, match, return_re)) {
            condition = match[1].str();
        }
        action.summary = "Wait until " +
            condition_description(condition, facts, attached_entity);
        action.event.kind = contains_ci(condition, "interact")
            ? QuestEventKind::Interaction : QuestEventKind::Condition;
        action.event.condition = condition_description(
            condition, facts, attached_entity);
        if (action.event.kind == QuestEventKind::Interaction) {
            action.event.actor = "Hero";
            action.event.target = humanize(attached_entity);
        }
        return action;
    }

    const std::vector<std::string> marker_calls = {
        "MoveAndRotateEntityToMarkerNamed", "MoveAndRotateToMarkerNamed",
        "MoveToMarker",
        "TeleportToMarker", "TeleportPlayerTo", "SetToMarker"};
    for (const std::string& call : marker_calls) {
        if (statement.find(call) == std::string::npos) continue;
        const auto args = call_arguments(statement, call);
        std::string marker;
        for (const std::string& arg : args) {
            marker = resolve_string(arg, facts);
            if (!marker.empty()) break;
        }
        const bool attached_actor_call = call == "MoveAndRotateToMarkerNamed";
        std::string actor = call == "TeleportPlayerTo"
            ? "Hero" : humanize(attached_entity);
        if (!args.empty() && call != "TeleportPlayerTo" &&
            !attached_actor_call) {
            actor = entity_name(args.front(), facts, attached_entity);
        }
        const bool teleport = contains_ci(call, "Teleport") || call == "SetToMarker";
        action.summary = std::string(teleport ? "Move " : "Guide ") + actor +
                         " to marker " + humanize(marker.empty() ? "target marker" : marker);
        action.event.kind = QuestEventKind::ActorMove;
        action.event.actor = actor;
        action.event.target = marker;
        if (!marker.empty()) resolve_world_marker(action, catalog, marker);
        return action;
    }

    if (statement.find("StartScriptControlledMovement") !=
        std::string::npos) {
        const auto args = call_arguments(
            statement, "StartScriptControlledMovement");
        const std::string actor = args.empty()
            ? humanize(attached_entity)
            : entity_name(args.front(), facts, attached_entity);
        action.event.kind = QuestEventKind::ActorMove;
        action.event.actor = actor;

        const std::size_t marker_call = statement.find("GetPositionOfEntity");
        if (marker_call != std::string::npos) {
            const auto marker_args = call_arguments(
                statement.substr(marker_call), "GetPositionOfEntity");
            const std::string marker = marker_args.empty()
                ? std::string() : resolve_string(marker_args.front(), facts);
            action.summary = "Guide " + actor + " to marker " + humanize(
                marker.empty() ? "target marker" : marker);
            action.event.target = marker;
            if (!marker.empty()) resolve_world_marker(action, catalog, marker);
        } else {
            action.summary = "Move " + actor + " to a world position";
            if (const auto coordinate = coordinate_for(statement, facts)) {
                action.extra.push_back(
                    "Position: " + format_coordinate(*coordinate));
                set_world_position(action.event, *coordinate);
            }
        }
        return action;
    }

    if (statement.find("MoveToPosition") != std::string::npos ||
        statement.find("SetScriptMoveToMode") != std::string::npos ||
        statement.find("TeleportToPosition") != std::string::npos) {
        const std::string call = statement.find("MoveToPosition") != std::string::npos
            ? "MoveToPosition"
            : statement.find("SetScriptMoveToMode") != std::string::npos
                ? "SetScriptMoveToMode"
                : "TeleportToPosition";
        const auto args = call_arguments(statement, call);
        const std::string actor = args.empty()
            ? humanize(attached_entity)
            : entity_name(args.front(), facts, attached_entity);
        action.summary = (call == "TeleportToPosition" ? "Teleport " : "Move ") +
                         actor + " to a world position";
        action.event.kind = QuestEventKind::ActorMove;
        action.event.actor = actor;
        if (const auto coordinate = coordinate_for(statement, facts)) {
            action.extra.push_back("Position: " + format_coordinate(*coordinate));
            set_world_position(action.event, *coordinate);
        }
        return action;
    }

    if (statement.find("Follow") != std::string::npos &&
        statement.find('(') != std::string::npos) {
        const auto args = call_arguments(statement, "Follow");
        const std::string actor = args.empty() ? humanize(attached_entity)
                                               : entity_name(args[0], facts, attached_entity);
        const std::string target = args.size() >= 2
            ? entity_name(args[1], facts, attached_entity)
            : "the target";
        action.summary = actor + " follows " + target;
        action.event.kind = QuestEventKind::ActorMove;
        action.event.actor = actor;
        action.event.target = target;
        return action;
    }

    if (statement.find("PlayAnimation") != std::string::npos ||
        statement.find("ActionPlayAnim") != std::string::npos) {
        const auto strings = quoted_strings(statement);
        const std::string animation = strings.empty() ? "scripted animation"
                                                       : strings.back();
        action.summary = humanize(attached_entity) + " plays animation " +
                         humanize(animation);
        action.extra.push_back("Animation ID: " + animation);
        action.event.kind = QuestEventKind::ActorAnimation;
        action.event.actor = humanize(attached_entity);
        action.event.animation_id = animation;
        return action;
    }

    if (statement.find("Attack") != std::string::npos &&
        statement.find('(') != std::string::npos) {
        const auto args = call_arguments(statement, "Attack");
        const std::string actor = args.empty() ? humanize(attached_entity)
                                               : entity_name(args.front(), facts, attached_entity);
        const std::string target = args.size() >= 2
            ? entity_name(args[1], facts, attached_entity)
            : "the current target";
        action.summary = actor + " attacks " + target;
        action.event.kind = QuestEventKind::ActorState;
        action.event.actor = actor;
        action.event.target = target;
        action.event.properties.push_back("attack");
        return action;
    }
    if (statement.find("Kill") != std::string::npos &&
        statement.find('(') != std::string::npos) {
        const auto args = call_arguments(statement, "Kill");
        const std::string victim = args.empty()
            ? humanize(attached_entity)
            : entity_name(args.front(), facts, attached_entity);
        if (args.size() >= 2) {
            const std::string killer = entity_name(
                args[1], facts, attached_entity);
            action.summary = killer + " kills " + victim;
            action.event.actor = killer;
        } else {
            action.summary = "Kill " + victim;
        }
        action.event.kind = QuestEventKind::ActorState;
        action.event.target = victim;
        action.event.properties.push_back("killed");
        return action;
    }
    if (statement.find("SetAsInvulnerable") != std::string::npos) {
        const auto args = call_arguments(statement, "SetAsInvulnerable");
        const std::string actor = args.empty() ? humanize(attached_entity)
                                               : entity_name(args.front(), facts, attached_entity);
        const bool enabled = statement.find("false") == std::string::npos;
        action.summary = std::string(enabled ? "Protect " : "Make vulnerable: ") + actor;
        action.event.kind = QuestEventKind::ActorState;
        action.event.actor = actor;
        action.event.properties.push_back(enabled ? "invulnerable" : "vulnerable");
        return action;
    }

    if (statement.find("CreateEntityFromDefinition") != std::string::npos ||
        statement.find("CreateEntityFromFactory") != std::string::npos) {
        const auto strings = quoted_strings(statement);
        const std::string definition = strings.empty() ? "entity" : strings.front();
        action.summary = "Spawn " + humanize(definition);
        action.extra.push_back("Entity definition: " + definition);
        action.event.kind = QuestEventKind::Spawn;
        action.event.target = definition;
        if (const auto coordinate = coordinate_for(statement, facts)) {
            action.extra.push_back("Spawn position: " + format_coordinate(*coordinate));
            set_world_position(action.event, *coordinate);
        }
        return action;
    }

    if (statement.find("Entity.Destroy") != std::string::npos ||
        statement.find(":Destroy()") != std::string::npos ||
        statement.find("DestroyEntity") != std::string::npos) {
        std::string call = statement.find("Entity.Destroy") != std::string::npos
            ? "Entity.Destroy" : "DestroyEntity";
        const auto args = call_arguments(statement, call);
        std::string target = args.empty() ? humanize(attached_entity)
                                          : entity_name(args.front(), facts,
                                                        attached_entity);
        action.summary = "Remove " + target + " from the world";
        action.event.kind = QuestEventKind::Despawn;
        action.event.target = target;
        return action;
    }

    if (statement.find("Door.SetOpen") != std::string::npos ||
        statement.find("Door.SetLocked") != std::string::npos) {
        const bool open_call = statement.find("SetOpen") != std::string::npos;
        const auto args = call_arguments(statement,
                                         open_call ? "Door.SetOpen"
                                                   : "Door.SetLocked");
        const std::string door = args.empty() ? "door"
                                              : entity_name(args.front(), facts, attached_entity);
        bool enabled = true;
        if (args.size() >= 2 && contains_ci(args[1], "false")) enabled = false;
        if (open_call) {
            action.summary = std::string(enabled ? "Open " : "Close ") + door;
            action.event.properties.push_back(enabled ? "open" : "closed");
        } else {
            action.summary = std::string(enabled ? "Lock " : "Unlock ") + door;
            action.event.properties.push_back(enabled ? "locked" : "unlocked");
        }
        action.event.kind = QuestEventKind::DoorState;
        action.event.target = door;
        return action;
    }

    const std::vector<std::pair<std::string, QuestEventKind>> inventory_calls = {
        {"Inventory.RemoveAllItemsOfType", QuestEventKind::InventoryRemove},
        {"Inventory.RemoveItemOfType", QuestEventKind::InventoryRemove},
        {"Inventory.RemoveItem", QuestEventKind::InventoryRemove},
        {"Inventory.AddItemOfType", QuestEventKind::InventoryAdd},
        {"Inventory.AddItem", QuestEventKind::InventoryAdd},
        {"Inventory.Clear", QuestEventKind::InventoryClear},
    };
    for (const auto& inventory_call : inventory_calls) {
        if (statement.find(inventory_call.first) == std::string::npos) continue;
        const auto args = call_arguments(statement, inventory_call.first);
        std::string owner = args.empty()
            ? "Hero" : entity_name(args.front(), facts, attached_entity);
        std::string item;
        if (args.size() >= 2) {
            item = resolve_string(args.back(), facts);
            if (item.empty()) item = entity_name(args.back(), facts,
                                                  attached_entity);
        } else if (!args.empty() && inventory_call.second ==
                                      QuestEventKind::InventoryClear) {
            item = owner;
        }
        action.event.kind = inventory_call.second;
        action.event.actor = owner;
        action.event.item = item;
        if (inventory_call.second == QuestEventKind::InventoryAdd) {
            action.summary = "Add " + humanize(item.empty() ? "quest item" : item) +
                             " to " + owner + "'s inventory";
        } else if (inventory_call.second == QuestEventKind::InventoryRemove) {
            action.summary = "Remove " +
                humanize(item.empty() ? "quest item" : item) + " from " +
                owner + "'s inventory";
        } else {
            action.summary = "Clear the inventory held by " + owner;
            action.event.target = owner;
        }
        if (!item.empty()) action.extra.push_back("Item ID: " + item);
        return action;
    }

    if (statement.find("Stats.ModifyMorality") != std::string::npos) {
        const auto args = call_arguments(statement, "Stats.ModifyMorality");
        const std::optional<double> amount = args.empty()
            ? std::nullopt : resolve_number(args.back(), facts);
        action.summary = "Change the Hero's morality";
        if (amount) {
            action.summary += " by " + format_number(*amount) +
                (*amount >= 0.0 ? " (good)" : " (evil)");
        }
        action.event.kind = QuestEventKind::Morality;
        action.event.actor = args.empty()
            ? "Hero" : entity_name(args.front(), facts, attached_entity);
        action.event.amount = amount;
        return action;
    }

    if (statement.find("Stats.ModifyPurity") != std::string::npos) {
        const auto args = call_arguments(statement, "Stats.ModifyPurity");
        const std::optional<double> amount = args.empty()
            ? std::nullopt : resolve_number(args.back(), facts);
        action.summary = "Change the Hero's purity";
        if (amount) action.summary += " by " + format_number(*amount);
        action.event.kind = QuestEventKind::Reward;
        action.event.actor = "Hero";
        action.event.item = "Purity";
        action.event.amount = amount;
        return action;
    }

    if (statement.find("Stats.ModifyRenown") != std::string::npos) {
        const auto args = call_arguments(statement, "Stats.ModifyRenown");
        const std::optional<double> amount = args.empty()
            ? std::nullopt : resolve_number(args.back(), facts);
        action.summary = amount
            ? "Reward: " + format_number(*amount) + " renown"
            : "Reward: renown";
        action.event.kind = QuestEventKind::Reward;
        action.event.actor = "Hero";
        action.event.item = "Renown";
        action.event.amount = amount;
        return action;
    }

    if (statement.find("Experience.Modify") != std::string::npos) {
        const auto args = call_arguments(statement, "Experience.Modify");
        std::string experience = "Experience";
        if (args.size() >= 2) {
            if (contains_ci(args[1], "EXPERIENCE_GENERAL")) {
                experience = "General experience";
            } else if (contains_ci(args[1], "EXPERIENCE_STRENGTH")) {
                experience = "Strength experience";
            } else if (contains_ci(args[1], "EXPERIENCE_SKILL")) {
                experience = "Skill experience";
            } else if (contains_ci(args[1], "EXPERIENCE_WILL")) {
                experience = "Will experience";
            }
        }
        const std::optional<double> amount = args.size() >= 3
            ? resolve_number(args[2], facts) : std::nullopt;
        action.summary = amount
            ? "Reward: " + format_number(*amount) + " " + experience
            : "Reward: " + experience;
        action.event.kind = QuestEventKind::Reward;
        action.event.actor = "Hero";
        action.event.item = experience;
        action.event.amount = amount;
        return action;
    }

    if (statement.find("Money.Modify") != std::string::npos) {
        const auto args = call_arguments(statement, "Money.Modify");
        const std::string amount_text = args.empty() ? std::string()
                                                      : trim(args.back());
        const std::optional<double> amount = resolve_number(amount_text, facts);
        if (amount && *amount >= 0.0) {
            action.summary = "Reward: " + format_number(*amount) + " gold";
        } else if (!amount_text.empty()) {
            action.summary = "Change the player's gold by " + amount_text;
        } else {
            action.summary = "Give the player gold";
        }
        action.event.kind = QuestEventKind::Reward;
        action.event.actor = "Hero";
        action.event.item = "gold";
        action.event.amount = amount;
        return action;
    }
    if (statement.find("GiveReward") != std::string::npos) {
        const auto strings = quoted_strings(statement);
        const std::string reward = strings.empty() ? "the scripted reward"
                                                    : strings.back();
        action.summary = "Reward: " + humanize(reward);
        action.event.kind = QuestEventKind::Reward;
        action.event.actor = "Hero";
        action.event.item = reward;
        return action;
    }

    if (statement.find('=') == std::string::npos) {
        if (const auto coordinate = parse_coordinate(statement)) {
            action.summary = "Use world position " + format_coordinate(*coordinate);
            action.event.kind = QuestEventKind::ScriptCall;
            set_world_position(action.event, *coordinate);
            return action;
        }
    }

    return action;
}

std::optional<QuestEvent> describe_fallback_event(
    const std::string& statement) {
    const std::string line = trim(statement);
    if (line.empty() || line.rfind("--", 0) == 0 || line == "end" ||
        line == "else") {
        return std::nullopt;
    }

    QuestEvent event;
    event.source_statement = statement;
    static const std::regex condition_re(
        R"condition(^(?:if|elseif|while)\s+(.+?)(?:\s+then|\s+do)?$)condition");
    std::smatch match;
    if (std::regex_match(line, match, condition_re)) {
        event.kind = QuestEventKind::Condition;
        event.condition = trim(match[1].str());
        event.title = "Evaluate condition: " + humanize(event.condition);
        return event;
    }

    static const std::regex state_assignment_re(
        R"assign(^(?:local\s+)?(self\.[A-Za-z_][A-Za-z0-9_\.]*)\s*=\s*(.+)$)assign");
    if (std::regex_match(line, match, state_assignment_re)) {
        event.kind = QuestEventKind::ActorState;
        event.target = match[1].str();
        event.title = "Set " + humanize(event.target);
        event.properties.push_back("Value: " + trim(match[2].str()));
        return event;
    }

    static const std::regex call_re(
        R"call(([A-Za-z_][A-Za-z0-9_\.:]*)\s*\()call");
    static const std::unordered_set<std::string> ignored_calls = {
        "print", "assert", "error", "type", "tonumber", "tostring",
        "pairs", "ipairs", "next", "select", "unpack",
    };
    std::vector<std::string> calls;
    for (std::sregex_iterator it(line.begin(), line.end(), call_re), end;
         it != end; ++it) {
        const std::string call = (*it)[1].str();
        if (ignored_calls.count(lower_ascii(call))) continue;
        if (std::find(calls.begin(), calls.end(), call) == calls.end()) {
            calls.push_back(call);
        }
    }
    if (calls.empty()) return std::nullopt;
    event.kind = QuestEventKind::ScriptCall;
    event.target = calls.front();
    event.title = "Run " + humanize(calls.front());
    for (const std::string& call : calls) {
        event.properties.push_back("Call: " + call);
    }
    return event;
}

void merge_narrative(Narrative& target, const Narrative& source) {
    target.actions.insert(target.actions.end(), source.actions.begin(),
                          source.actions.end());
    target.supplemental_events.insert(target.supplemental_events.end(),
                                      source.supplemental_events.begin(),
                                      source.supplemental_events.end());
    for (int value : source.transitions) {
        if (std::find(target.transitions.begin(), target.transitions.end(), value) ==
            target.transitions.end()) target.transitions.push_back(value);
    }
    for (const auto& value : source.entities) append_unique(target.entities, value);
    for (const auto& value : source.locations) append_unique(target.locations, value);
    for (const auto& value : source.markers) append_unique(target.markers, value);
    for (const auto& value : source.coordinates) append_unique(target.coordinates, value);
    for (const auto& value : source.self_calls) append_unique(target.self_calls, value);
    target.terminal = target.terminal || source.terminal;
}

Narrative collect_narrative(const std::vector<std::string>& lines,
                            std::size_t begin, std::size_t end,
                            const std::string& attached_entity,
                            const ScriptFacts& facts,
                            const ReferenceCatalog& catalog,
                            std::size_t action_limit = 24,
                            const std::string& source_class = {},
                            const std::string& source_method = {},
                            int source_state = -1) {
    Narrative result;
    static const std::regex self_call_re(R"(self:([A-Za-z_][A-Za-z0-9_]*)\s*\()");
    for (std::size_t i = begin; i < end && i < lines.size();) {
        const std::size_t source_line = i + 1;
        auto [statement, last] = gather_statement(lines, i, end);
        i = last + 1;
        if (statement.empty() || statement.rfind("--", 0) == 0) continue;

        for (std::sregex_iterator it(statement.begin(), statement.end(), self_call_re),
             finish; it != finish; ++it) {
            append_unique(result.self_calls, (*it)[1].str());
        }
        for (const auto& pair : facts.entities) {
            if (statement.find(pair.first) != std::string::npos) {
                append_unique(result.entities, humanize(pair.second));
            }
        }
        if (statement.find("QuestManager.HeroEntity") != std::string::npos ||
            statement.find("GetPlayerHero()") != std::string::npos) {
            append_unique(result.entities, "Hero");
        }

        const std::vector<std::string> strings = quoted_strings(statement);
        if (statement.find("GetEntityWithName") != std::string::npos &&
            !strings.empty()) append_unique(result.entities, humanize(strings.front()));
        if (statement.find("StartNewEntityThread") != std::string::npos &&
            !strings.empty()) append_unique(result.entities, humanize(strings.front()));
        if (contains_ci(statement, "Marker") && !strings.empty()) {
            append_unique(result.markers, strings.back());
        }
        if (statement.find("LoadAndWaitForLevel") != std::string::npos ||
            statement.find("Debug.LoadLevel") != std::string::npos ||
            statement.find("TeleportToLevel") != std::string::npos) {
            for (const std::string& value : strings) {
                if (!value.empty()) append_unique(result.locations, humanize(value));
            }
        }
        for (const std::string& value : strings) {
            if (value.size() < 4) continue;
            for (const std::string& level : catalog.level_assets) {
                if (contains_ci(level, value)) {
                    append_unique(result.locations, humanize(value));
                    break;
                }
            }
        }
        if (const auto coordinate = coordinate_for(statement, facts)) {
            append_unique(result.coordinates, format_coordinate(*coordinate));
        }

        NarrativeAction action = describe_statement(
            statement, attached_entity, facts, catalog);
        action.event.title = action.summary;
        action.event.source_class = source_class;
        action.event.source_method = source_method;
        action.event.source_state = source_state;
        action.event.source_line = source_line;
        if (!action.summary.empty() &&
            action.event.kind == QuestEventKind::Unknown) {
            action.event.kind = QuestEventKind::ScriptCall;
        }
        if (action.transition) {
            int target = *action.transition;
            if (target != -2147483647 &&
                std::find(result.transitions.begin(), result.transitions.end(), target) ==
                    result.transitions.end()) result.transitions.push_back(target);
        }
        if (!action.summary.empty() && result.actions.size() < action_limit) {
            result.terminal = result.terminal || action.terminal;
            result.actions.push_back(std::move(action));
        } else if (!action.summary.empty()) {
            result.terminal = result.terminal || action.terminal;
            result.supplemental_events.push_back(std::move(action.event));
        } else if (auto fallback = describe_fallback_event(statement)) {
            fallback->source_class = source_class;
            fallback->source_method = source_method;
            fallback->source_state = source_state;
            fallback->source_line = source_line;
            result.supplemental_events.push_back(std::move(*fallback));
        }
    }
    return result;
}

std::vector<StateBranch> find_states(const FunctionBlock& function,
                                     const std::vector<std::string>& lines) {
    static const std::regex state_re(
        R"(^\s*(?:if|elseif)\s+self\.CurrentState\s*==\s*(-?[0-9]+)\s+then)");
    std::vector<StateBranch> branches;
    for (std::size_t i = function.begin + 1; i < function.end; ++i) {
        std::smatch match;
        if (!std::regex_search(lines[i], match, state_re)) continue;
        if (!branches.empty()) branches.back().end = i;
        StateBranch branch;
        branch.value = std::stoi(match[1].str());
        branch.begin = i;
        branch.end = function.end;
        branches.push_back(branch);
    }
    return branches;
}

GraphNode& add_node(Graph& graph, NodeKind kind, std::string title,
                    std::string subtitle, float x, float y) {
    GraphNode node;
    node.id = static_cast<int>(graph.nodes.size()) + 1;
    node.kind = kind;
    node.title = std::move(title);
    node.subtitle = std::move(subtitle);
    node.x = x;
    node.y = y;
    graph.nodes.push_back(std::move(node));
    return graph.nodes.back();
}

GraphNode* node_by_id(Graph& graph, int id) {
    for (GraphNode& node : graph.nodes) {
        if (node.id == id) return &node;
    }
    return nullptr;
}

void add_link(Graph& graph,
              std::set<std::tuple<int, int, std::string>>& unique_links,
              int from, int to, std::string label, bool inferred = false) {
    if (from <= 0 || to <= 0 || from == to) return;
    auto key = std::make_tuple(from, to, label);
    if (!unique_links.insert(key).second) return;
    GraphLink link;
    link.id = static_cast<int>(graph.links.size()) + 1;
    link.from_node = from;
    link.to_node = to;
    link.label = std::move(label);
    link.inferred = inferred;
    graph.links.push_back(std::move(link));
}

std::string join(const std::vector<std::string>& values,
                 const std::string& separator, std::size_t limit = 8) {
    std::string result;
    for (std::size_t i = 0; i < values.size() && i < limit; ++i) {
        if (!result.empty()) result += separator;
        result += values[i];
    }
    if (values.size() > limit) result += separator + "+" +
        std::to_string(values.size() - limit) + " more";
    return result;
}

void append_narrative_details(GraphNode& node, const Narrative& narrative,
                              std::size_t action_limit = 12,
                              const std::string& indent = {}) {
    for (const NarrativeAction& action : narrative.actions) {
        node.events.push_back(action.event);
    }
    node.events.insert(node.events.end(), narrative.supplemental_events.begin(),
                       narrative.supplemental_events.end());
    for (std::size_t i = 0;
         i < narrative.actions.size() && i < action_limit; ++i) {
        const NarrativeAction& action = narrative.actions[i];
        node.details.push_back(indent + std::to_string(i + 1) + ". " +
                               action.summary);
        for (const std::string& extra : action.extra) {
            node.details.push_back(indent + "   " + extra);
        }
    }
    if (narrative.actions.size() > action_limit) {
        node.details.push_back(indent + "+ " +
            std::to_string(narrative.actions.size() - action_limit) +
            " more actions; see Lua Script for the complete source");
    }
    if (!narrative.entities.empty()) {
        node.details.push_back(indent + "NPCs/entities: " +
                               join(narrative.entities, ", "));
    }
    if (!narrative.locations.empty()) {
        node.details.push_back(indent + "Areas/levels: " +
                               join(narrative.locations, ", "));
    }
    if (!narrative.markers.empty()) {
        node.details.push_back(indent + "Markers: " +
                               join(narrative.markers, ", "));
    }
    if (!narrative.coordinates.empty()) {
        node.details.push_back(indent + "Coordinates: " +
                               join(narrative.coordinates, " | "));
    }
}

void count_references(Graph& graph, const Narrative& narrative) {
    for (const NarrativeAction& action : narrative.actions) {
        graph.dialogue_lines += action.dialogue_lines;
        graph.audio_matches += action.audio_matches;
    }
}

std::string narrative_headline(const Narrative& narrative,
                               const std::string& fallback) {
    for (const NarrativeAction& action : narrative.actions) {
        if (!action.summary.empty()) return shorten(action.summary, 76);
    }
    if (!narrative.markers.empty()) return "Reach " + humanize(narrative.markers.front());
    if (!narrative.locations.empty()) return "Continue in " + narrative.locations.front();
    return fallback;
}

std::pair<float, float> main_flow_position(std::size_t index) {
    constexpr std::size_t columns = 5;
    constexpr float start_x = 360.0f;
    constexpr float dx = 390.0f;
    constexpr float dy = 390.0f;
    const std::size_t row = index / columns;
    std::size_t column = index % columns;
    if ((row & 1u) != 0) column = columns - 1 - column;
    return {start_x + float(column) * dx, float(row) * dy};
}

const FunctionBlock* find_function(const std::vector<FunctionBlock>& functions,
                                   const std::string& class_name,
                                   const std::string& method) {
    for (const FunctionBlock& function : functions) {
        if (function.class_name == class_name && function.method == method) {
            return &function;
        }
    }
    return nullptr;
}

std::string function_key(const std::string& class_name,
                         const std::string& method) {
    return class_name + "::" + method;
}

}

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

const char* NodeKindName(NodeKind kind) {
    switch (kind) {
        case NodeKind::Quest: return "Quest start";
        case NodeKind::Thread: return "NPC / entity behaviour";
        case NodeKind::Function: return "Supporting quest logic";
        case NodeKind::State: return "Quest step";
        case NodeKind::Action: return "Quest action";
    }
    return "Quest node";
}

const char* QuestEventKindName(QuestEventKind kind) {
    switch (kind) {
        case QuestEventKind::Unknown: return "Unknown";
        case QuestEventKind::Dialogue: return "Dialogue";
        case QuestEventKind::Cutscene: return "Cutscene";
        case QuestEventKind::Interaction: return "Interaction";
        case QuestEventKind::Condition: return "Condition";
        case QuestEventKind::ObjectiveSet: return "Objective set";
        case QuestEventKind::ObjectiveRemove: return "Objective removed";
        case QuestEventKind::ObjectiveComplete: return "Objective complete";
        case QuestEventKind::ObjectiveTarget: return "Objective target";
        case QuestEventKind::InventoryAdd: return "Inventory add";
        case QuestEventKind::InventoryRemove: return "Inventory remove";
        case QuestEventKind::InventoryClear: return "Inventory clear";
        case QuestEventKind::DigSpotEnable: return "Dig spot enabled";
        case QuestEventKind::DigSpotComplete: return "Dig completed";
        case QuestEventKind::TimerStart: return "Timer started";
        case QuestEventKind::TimerWait: return "Timer wait";
        case QuestEventKind::TimerStop: return "Timer stopped";
        case QuestEventKind::ActorMove: return "Actor movement";
        case QuestEventKind::ActorAnimation: return "Actor animation";
        case QuestEventKind::ActorState: return "Actor state";
        case QuestEventKind::Spawn: return "Spawn";
        case QuestEventKind::Despawn: return "Despawn";
        case QuestEventKind::DoorState: return "Door state";
        case QuestEventKind::LayerState: return "Layer state";
        case QuestEventKind::Camera: return "Camera";
        case QuestEventKind::Travel: return "Travel";
        case QuestEventKind::Reward: return "Reward";
        case QuestEventKind::Morality: return "Morality";
        case QuestEventKind::QuestStart: return "Quest start";
        case QuestEventKind::QuestComplete: return "Quest complete";
        case QuestEventKind::QuestFail: return "Quest failed";
        case QuestEventKind::ScriptCall: return "Script call";
    }
    return "Unknown";
}

Graph BuildGraph(const std::string& title, const std::string& decompiled_lua) {
    return BuildGraph(title, decompiled_lua, ReferenceCatalog{});
}

Graph BuildGraph(const std::string& title, const std::string& decompiled_lua,
                 const ReferenceCatalog& references) {
    Graph graph;
    graph.title = title;
    const std::vector<std::string> lines = split_lines(decompiled_lua);
    if (lines.empty()) return graph;

    const std::regex thread_re(
        R"quest(^\s*QuestManager\.New(Quest|Entity)Thread\("([^"]+)"\))quest");
    const std::regex function_re(
        R"(^\s*function\s+([A-Za-z0-9_\.]+):([A-Za-z0-9_]+)\s*\()");

    std::vector<ThreadDecl> threads;
    std::unordered_set<std::string> declared_threads;
    for (const std::string& line : lines) {
        std::smatch match;
        if (!std::regex_search(line, match, thread_re)) continue;
        const std::string class_name = match[2].str();
        if (!declared_threads.insert(class_name).second) continue;
        const bool entity = match[1].str() == "Entity";
        threads.push_back({class_name, entity});
        if (entity) ++graph.entity_threads;
        else ++graph.quest_threads;
    }

    std::vector<FunctionBlock> functions;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        std::smatch match;
        if (!std::regex_search(lines[i], match, function_re)) continue;
        if (!functions.empty()) functions.back().end = i;
        FunctionBlock function;
        function.class_name = match[1].str();
        function.method = match[2].str();
        function.begin = i;
        function.end = lines.size();
        functions.push_back(std::move(function));
    }
    graph.functions = functions.size();

    const ScriptFacts facts = build_facts(lines);
    std::string root_class;
    for (const ThreadDecl& thread : threads) {
        if (!thread.entity) {
            root_class = thread.class_name;
            break;
        }
    }
    if (root_class.empty() && !functions.empty()) root_class = functions.front().class_name;

    GraphNode& root = add_node(
        graph, NodeKind::Quest,
        humanize(root_class.empty() ? title : root_class),
        "Quest flow begins here", 0.0f, 0.0f);
    const int root_id = root.id;
    root.details.push_back("Script: " + title);
    if (!root_class.empty()) root.details.push_back("Main controller: " + root_class);
    root.details.push_back("Follow the connected quest steps in arrow order.");

    if (const FunctionBlock* init = find_function(functions, root_class, "Init")) {
        Narrative setup = collect_narrative(lines, init->begin + 1, init->end,
                                            "Quest", facts, references, 8,
                                            root_class, "Init");
        if (!setup.actions.empty() || !setup.supplemental_events.empty() ||
            !setup.entities.empty() ||
            !setup.locations.empty() || !setup.markers.empty() ||
            !setup.coordinates.empty()) {
            root.details.push_back("Initial setup:");
            append_narrative_details(root, setup, 6, "  ");
            count_references(graph, setup);
        }
    }

    std::set<std::tuple<int, int, std::string>> unique_links;
    std::unordered_map<std::string, std::vector<std::pair<int, std::string>>>
        thread_starts;
    std::unordered_map<std::string, std::vector<int>> helper_sources;
    std::unordered_map<int, Narrative> flow_narratives;

    const FunctionBlock* main_update = find_function(functions, root_class, "Update");
    std::vector<StateBranch> main_states;
    if (main_update) main_states = find_states(*main_update, lines);

    if (!main_states.empty()) {
        std::unordered_map<int, int> state_nodes;
        for (std::size_t i = 0; i < main_states.size(); ++i) {
            StateBranch& state = main_states[i];
            Narrative narrative = collect_narrative(
                lines, state.begin + 1, state.end, "Quest", facts, references,
                256, root_class, "Update", state.value);
            for (NarrativeAction& action : narrative.actions) {
                if (action.transition && *action.transition == -2147483647) {
                    action.transition = state.value + 1;
                    narrative.transitions.push_back(state.value + 1);
                }
            }
            auto [x, y] = main_flow_position(i);
            GraphNode& node = add_node(
                graph, NodeKind::State,
                "Step " + std::to_string(i + 1) + " - " +
                    narrative_headline(narrative, "Continue quest logic"),
                "Main quest | script state " + std::to_string(state.value), x, y);
            state.node_id = node.id;
            state_nodes[state.value] = node.id;
            append_narrative_details(node, narrative, 256);
            node.details.push_back("Source: " + root_class + ":Update, state " +
                                   std::to_string(state.value));
            flow_narratives[node.id] = narrative;
            for (const NarrativeAction& action : narrative.actions) {
                if (!action.starts_thread.empty()) {
                    thread_starts[action.starts_thread].push_back(
                        {node.id, action.entity_instance});
                }
            }
            for (const std::string& method : narrative.self_calls) {
                helper_sources[function_key(root_class, method)].push_back(node.id);
            }
            count_references(graph, narrative);
            ++graph.states;
            ++graph.flow_steps;
        }

        add_link(graph, unique_links, root_id, main_states.front().node_id,
                 "begins");
        for (std::size_t i = 0; i < main_states.size(); ++i) {
            StateBranch& state = main_states[i];
            Narrative& narrative = flow_narratives[state.node_id];
            std::vector<int> valid_targets;
            for (int target : narrative.transitions) {
                if (std::find(valid_targets.begin(), valid_targets.end(), target) ==
                    valid_targets.end() && state_nodes.count(target)) {
                    valid_targets.push_back(target);
                }
            }
            bool inferred = false;
            if (valid_targets.empty() && !narrative.terminal &&
                i + 1 < main_states.size() &&
                main_states[i + 1].value == state.value + 1) {
                valid_targets.push_back(main_states[i + 1].value);
                inferred = true;
            }
            GraphNode* node = node_by_id(graph, state.node_id);
            for (int target : valid_targets) {
                add_link(graph, unique_links, state.node_id, state_nodes[target],
                         inferred ? "next state (inferred)" : "then", inferred);
                if (node) {
                    node->details.push_back(
                        std::string(inferred ? "Likely next: " : "Next: ") +
                        "script state " + std::to_string(target) +
                        (inferred ? " (transition is indirect)" : ""));
                }
            }
        }
    } else if (main_update) {
        Narrative narrative = collect_narrative(
            lines, main_update->begin + 1, main_update->end,
            "Quest", facts, references, 256, root_class, "Update");
        int previous = root_id;
        std::vector<std::pair<std::size_t, int>> action_nodes;
        for (std::size_t i = 0; i < narrative.actions.size(); ++i) {
            const NarrativeAction& action = narrative.actions[i];
            auto [x, y] = main_flow_position(i);
            GraphNode& node = add_node(
                graph, NodeKind::Action,
                "Step " + std::to_string(i + 1) + " - " +
                    shorten(action.summary, 76),
                "Main quest action", x, y);
            node.details.push_back(action.summary);
            for (const std::string& extra : action.extra) node.details.push_back(extra);
            node.events.push_back(action.event);
            action_nodes.push_back({action.event.source_line, node.id});
            add_link(graph, unique_links, previous, node.id, "then");
            previous = node.id;
            if (!action.starts_thread.empty()) {
                thread_starts[action.starts_thread].push_back(
                    {node.id, action.entity_instance});
            }
            ++graph.flow_steps;
        }
        for (const QuestEvent& event : narrative.supplemental_events) {
            int owner = root_id;
            for (const auto& candidate : action_nodes) {
                if (candidate.first > event.source_line) break;
                owner = candidate.second;
            }
            if (GraphNode* node = node_by_id(graph, owner)) {
                node->events.push_back(event);
            }
        }
        count_references(graph, narrative);
    }

    const std::size_t main_rows =
        std::max<std::size_t>(1, (graph.flow_steps + 4) / 5);
    float support_y = float(main_rows) * 390.0f + 140.0f;
    std::unordered_map<std::string, int> thread_nodes;
    std::unordered_map<std::string, std::string> class_actor;

    for (const ThreadDecl& thread : threads) {
        if (!thread.entity && thread.class_name == root_class) continue;
        std::string actor;
        const auto starts = thread_starts.find(thread.class_name);
        if (starts != thread_starts.end()) {
            for (const auto& source : starts->second) {
                if (!source.second.empty()) {
                    actor = source.second;
                    break;
                }
            }
        }
        if (actor.empty()) actor = humanize(thread.class_name);
        class_actor[thread.class_name] = actor;

        Narrative behaviour;
        Narrative setup_narrative;
        std::vector<std::pair<int, Narrative>> phases;
        if (const FunctionBlock* init = find_function(functions, thread.class_name, "Init")) {
            setup_narrative = collect_narrative(
                lines, init->begin + 1, init->end, actor, facts, references, 8,
                thread.class_name, "Init");
            merge_narrative(behaviour, setup_narrative);
        }
        std::string behaviour_method = "CustomUpdate";
        const FunctionBlock* update =
            find_function(functions, thread.class_name, behaviour_method);
        if (!update) {
            behaviour_method = "Update";
            update = find_function(functions, thread.class_name, behaviour_method);
        }
        if (update) {
            const std::vector<StateBranch> states = find_states(*update, lines);
            if (states.empty()) {
                Narrative body = collect_narrative(
                    lines, update->begin + 1, update->end, actor,
                    facts, references, 256, thread.class_name,
                    behaviour_method);
                merge_narrative(behaviour, body);
            } else {
                graph.states += states.size();
                for (const StateBranch& state : states) {
                    Narrative phase = collect_narrative(
                        lines, state.begin + 1, state.end, actor,
                        facts, references, 512, thread.class_name,
                        behaviour_method, state.value);
                    phases.push_back({state.value, phase});
                    merge_narrative(behaviour, phase);
                }
            }
        }

        GraphNode& node = add_node(
            graph, thread.entity ? NodeKind::Thread : NodeKind::Function,
            humanize(actor) + (thread.entity ? " - behaviour" : " - supporting flow"),
            std::string(thread.entity ? "NPC/entity" : "Quest sub-thread") +
                " | " + thread.class_name,
            360.0f, support_y);
        const int thread_node_id = node.id;
        thread_nodes[thread.class_name] = thread_node_id;
        node.details.push_back(std::string(thread.entity ? "Attached entity: "
                                                         : "Thread: ") + actor);
        if (!phases.empty()) {
            node.details.push_back(
                std::to_string(phases.size()) +
                " connected phases; follow the arrows from left to right.");
            if (!setup_narrative.actions.empty() ||
                !setup_narrative.supplemental_events.empty() ||
                !setup_narrative.entities.empty() ||
                !setup_narrative.locations.empty() ||
                !setup_narrative.markers.empty() ||
                !setup_narrative.coordinates.empty()) {
                node.details.push_back("Initial setup:");
                append_narrative_details(node, setup_narrative, 6, "  ");
                count_references(graph, setup_narrative);
            }
            for (const std::string& method : setup_narrative.self_calls) {
                helper_sources[function_key(thread.class_name, method)]
                    .push_back(thread_node_id);
            }

            int previous_phase = thread_node_id;
            for (std::size_t p = 0; p < phases.size(); ++p) {
                constexpr std::size_t columns = 5;
                const std::size_t row = p / columns;
                std::size_t column = p % columns;
                if ((row & 1u) != 0) column = columns - 1 - column;
                GraphNode& phase = add_node(
                    graph, NodeKind::State,
                    humanize(actor) + " - Phase " + std::to_string(p + 1) +
                        ": " + narrative_headline(
                            phases[p].second, "Continue behaviour"),
                    "NPC/entity sequence | script state " +
                        std::to_string(phases[p].first),
                    790.0f + float(column) * 430.0f,
                    support_y + float(row) * 900.0f);
                append_narrative_details(phase, phases[p].second, 512);
                phase.details.push_back("Source: " + thread.class_name + ":" +
                                        behaviour_method + ", state " +
                                        std::to_string(phases[p].first));
                add_link(graph, unique_links, previous_phase, phase.id,
                         p == 0 ? "begins behaviour" : "then");
                previous_phase = phase.id;
                count_references(graph, phases[p].second);
                for (const std::string& method : phases[p].second.self_calls) {
                    helper_sources[function_key(thread.class_name, method)]
                        .push_back(phase.id);
                }
            }
        } else {
            append_narrative_details(node, behaviour, 256);
            count_references(graph, behaviour);
            for (const std::string& method : behaviour.self_calls) {
                helper_sources[function_key(thread.class_name, method)]
                    .push_back(thread_node_id);
            }
        }
        GraphNode* thread_node = node_by_id(graph, thread_node_id);
        if (thread_node && behaviour.actions.empty() && behaviour.entities.empty() &&
            behaviour.locations.empty() && behaviour.markers.empty()) {
            thread_node->details.push_back(
                "No dialogue, movement, combat, wait, or location action was "
                "recognized in this thread.");
        }
        if (thread_node) {
            thread_node->details.push_back("Source class: " + thread.class_name);
        }

        if (starts != thread_starts.end()) {
            for (const auto& source : starts->second) {
                add_link(graph, unique_links, source.first, thread_node_id,
                         "starts " + humanize(actor));
            }
        } else {
            add_link(graph, unique_links, root_id, thread_node_id,
                     "supporting behaviour");
        }
        const std::size_t phase_rows = phases.empty()
            ? 1 : std::max<std::size_t>(1, (phases.size() + 4) / 5);
        support_y += float(phase_rows) * 900.0f + 140.0f;
    }

    support_y += 100.0f;
    std::size_t helper_index = 0;
    for (const FunctionBlock& function : functions) {
        if (function.method == "Init" || function.method == "Update" ||
            function.method == "CustomUpdate" ||
            function.method == "OnTerminated") continue;
        const std::string key = function_key(function.class_name, function.method);
        std::string actor = "Quest";
        auto actor_it = class_actor.find(function.class_name);
        if (actor_it != class_actor.end()) actor = actor_it->second;
        Narrative narrative = collect_narrative(
            lines, function.begin + 1, function.end, actor,
            facts, references, 256, function.class_name, function.method);
        if (narrative.actions.empty() && narrative.supplemental_events.empty() &&
            narrative.entities.empty() &&
            narrative.locations.empty() && narrative.markers.empty() &&
            narrative.coordinates.empty()) continue;

        const std::size_t column = helper_index % 4;
        const std::size_t row = helper_index / 4;
        GraphNode& node = add_node(
            graph, NodeKind::Function, humanize(function.method),
            "Supporting action | " + humanize(actor),
            360.0f + float(column) * 430.0f,
            support_y + float(row) * 720.0f);
        append_narrative_details(node, narrative, 256);
        node.details.push_back("Source: " + function.class_name + ":" +
                               function.method);
        count_references(graph, narrative);

        auto sources = helper_sources.find(key);
        if (sources != helper_sources.end() && !sources->second.empty()) {
            for (int source : sources->second) {
                add_link(graph, unique_links, source, node.id, "runs");
            }
        } else {
            const auto owner = thread_nodes.find(function.class_name);
            add_link(graph, unique_links,
                     owner == thread_nodes.end() ? root_id : owner->second,
                     node.id, "supporting logic");
        }
        ++helper_index;
    }

    if (graph.flow_steps == 0) {
        if (GraphNode* root_node = node_by_id(graph, root_id)) {
            root_node->details.push_back(
                "No ordered main Update flow was recognized; supporting "
                "behaviours are shown below.");
        }
    } else if (GraphNode* root_node = node_by_id(graph, root_id)) {
        root_node->details.push_back(
            std::to_string(graph.flow_steps) + " main quest steps | " +
            std::to_string(graph.entity_threads) + " entity behaviours");
    }

    return graph;
}

}
