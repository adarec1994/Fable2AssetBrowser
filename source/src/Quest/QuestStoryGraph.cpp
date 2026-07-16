#include "QuestStoryGraph.h"

#include <algorithm>
#include <cctype>
#include <deque>
#include <iomanip>
#include <iterator>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace Quest {
namespace {

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

bool parse_dialogue(const std::string& line, std::string& tag,
                    std::string& speaker, std::string& text) {
    if (!starts_ci(line, "Dialogue [")) return false;
    const std::size_t close = line.find(']');
    if (close == std::string::npos) return false;
    tag = line.substr(std::string("Dialogue [").size(),
                      close - std::string("Dialogue [").size());
    std::string rest = trim(line.substr(close + 1));
    if (!rest.empty() && rest.front() == ':') {
        text = trim(rest.substr(1));
        return !text.empty();
    }
    const std::size_t colon = rest.find(':');
    if (colon == std::string::npos) {
        text = rest;
        return !text.empty();
    }
    speaker = clean_names(rest.substr(0, colon));
    text = trim(rest.substr(colon + 1));
    return !text.empty();
}

bool parse_direct_dialogue(const std::string& line, Beat& beat) {
    const std::string marker = " says: \"";
    const std::size_t says = line.find(marker);
    if (says == std::string::npos) return false;
    std::string text = line.substr(says + marker.size());
    if (!text.empty() && text.back() == '"') text.pop_back();
    const std::string speaker = clean_names(line.substr(0, says));
    beat.kind = BeatKind::Dialogue;
    beat.title = speaker + ": " + text;
    return true;
}

bool classify_action(const std::string& line, Beat& beat) {
    if (starts_ci(line, "Camera event: ")) {
        beat.kind = BeatKind::Camera;
        beat.title = clean_names(line.substr(
            std::string("Camera event: ").size()));
        beat.subtitle = "Camera direction";
        return !beat.title.empty();
    }
    if (starts_ci(line, "Actor action: ")) {
        beat.kind = BeatKind::ActorAction;
        beat.title = clean_names(line.substr(
            std::string("Actor action: ").size()));
        beat.subtitle = "Actor staging";
        return !beat.title.empty();
    }
    if (starts_ci(line, "Set objective: ")) {
        const std::string objective = trim(line.substr(
            std::string("Set objective: ").size()));
        if (objective.empty()) return false;
        beat.kind = BeatKind::Objective;
        beat.title = "Objective: " + clean_names(objective);
        return true;
    }
    if (starts_ci(line, "Point the objective marker at ") ||
        starts_ci(line, "Set the objective destination to ")) {
        beat.kind = BeatKind::Objective;
        beat.title = clean_names(line);
        return true;
    }
    if (starts_ci(line, "Mark the current objective complete") ||
        starts_ci(line, "Complete the objective")) {
        beat.kind = BeatKind::Objective;
        beat.title = "Objective complete";
        return true;
    }
    if ((starts_ci(line, "Wait until ") ||
         starts_ci(line, "Wait for trigger ")) &&
        meaningful_condition(line)) {
        beat.kind = BeatKind::Decision;
        beat.title = decision_title(line);
        return true;
    }
    if (starts_ci(line, "Travel to ")) {
        beat.kind = BeatKind::Task;
        std::string destination = line.substr(std::string("Travel to ").size());
        const std::size_t first = destination.find('/');
        const std::size_t second = first == std::string::npos
            ? std::string::npos : destination.find('/', first + 1);
        if (second != std::string::npos) destination.resize(second);
        beat.title = "Travel to " + clean_names(destination);
        beat.subtitle = "Location";
        return true;
    }
    if (starts_ci(line, "Guide Hero to marker ")) {
        beat.kind = BeatKind::Task;
        beat.title = "Go to " + clean_names(line.substr(
            std::string("Guide Hero to marker ").size()));
        beat.subtitle = "Location";
        return true;
    }
    if (starts_ci(line, "Move ") || starts_ci(line, "Teleport ") ||
        starts_ci(line, "Guide ") || contains_ci(line, " plays animation ") ||
        contains_ci(line, " follows ") || contains_ci(line, " attacks ") ||
        contains_ci(line, " turns to face ") ||
        contains_ci(line, " starts looking at ") ||
        contains_ci(line, " stops looking at ")) {
        beat.kind = BeatKind::ActorAction;
        beat.title = clean_names(line);
        beat.subtitle = "Actor staging";
        return true;
    }
    if (starts_ci(line, "Spawn ")) {
        beat.kind = BeatKind::Task;
        beat.title = clean_names(line);
        beat.subtitle = "Target";
        return true;
    }
    if (starts_ci(line, "Kill ")) {
        beat.kind = BeatKind::Task;
        beat.title = "Defeat " + clean_names(line.substr(5));
        beat.subtitle = "Combat";
        return true;
    }
    if (contains_ci(line, " kills ")) {
        beat.kind = BeatKind::Task;
        beat.title = clean_names(line);
        beat.subtitle = "Story event";
        return true;
    }
    if (starts_ci(line, "Reward: ") ||
        starts_ci(line, "Give the player ")) {
        if (contains_ci(line, "scripted reward")) return false;
        beat.kind = BeatKind::Reward;
        beat.title = starts_ci(line, "Reward: ")
            ? clean_names(line)
            : "Reward: " + clean_names(line.substr(
                  std::string("Give the player ").size()));
        return true;
    }
    if (starts_ci(line, "Complete the quest")) {
        beat.kind = BeatKind::Ending;
        beat.title = "Quest complete";
        return true;
    }
    if (starts_ci(line, "Fail the quest")) {
        beat.kind = BeatKind::Ending;
        beat.title = "Quest failed";
        return true;
    }
    if (starts_ci(line, "Play cutscene ")) {
        std::string scene = line.substr(std::string("Play cutscene ").size());
        const std::size_t spoken = lower_ascii(scene).find(" (", 1);
        if (spoken != std::string::npos) scene.resize(spoken);
        beat.kind = BeatKind::Task;
        beat.title = "Cutscene: " + clean_names(scene);
        beat.subtitle = "Story event";
        return true;
    }
    if (starts_ci(line, "Play movie ")) {
        beat.kind = BeatKind::Task;
        beat.title = "Cinematic: " + clean_names(line.substr(
            std::string("Play movie ").size()));
        beat.subtitle = "Story event";
        return true;
    }
    return false;
}

bool classify_event(const QuestEvent& event, const std::string& line,
                    Beat& beat) {
    beat.title = clean_names(event.title.empty() ? line : event.title);
    switch (event.kind) {
        case QuestEventKind::Interaction:
            beat.kind = BeatKind::Interaction;
            beat.subtitle = "Player interaction";
            return true;
        case QuestEventKind::InventoryAdd:
        case QuestEventKind::InventoryRemove:
        case QuestEventKind::InventoryClear:
            beat.kind = BeatKind::Inventory;
            beat.subtitle = "Quest inventory change";
            return true;
        case QuestEventKind::DigSpotEnable:
            beat.kind = BeatKind::WorldState;
            beat.subtitle = "Dig spot";
            return true;
        case QuestEventKind::DigSpotComplete:
            beat.kind = BeatKind::Interaction;
            beat.subtitle = "Dig interaction";
            return true;
        case QuestEventKind::TimerStart:
        case QuestEventKind::TimerWait:
        case QuestEventKind::TimerStop:
            beat.kind = BeatKind::Timer;
            beat.subtitle = "Timed quest event";
            return true;
        case QuestEventKind::DoorState:
        case QuestEventKind::LayerState:
        case QuestEventKind::Despawn:
        case QuestEventKind::Morality:
            beat.kind = BeatKind::WorldState;
            beat.subtitle = "World state change";
            return true;
        default:
            return false;
    }
}

bool is_completion_node(const GraphNode& node) {
    for (const QuestEvent& event : node.events) {
        if (event.kind == QuestEventKind::QuestComplete) return true;
        if (event.kind == QuestEventKind::QuestFail) return false;
    }
    if (contains_ci(node.badge, "failed") ||
        contains_ci(node.title, "failed")) {
        return false;
    }
    if (contains_ci(node.badge, "ending choice")) return false;
    if (contains_ci(node.badge, "ending") ||
        contains_ci(node.badge, "quest end")) {
        return true;
    }
    for (const std::string& detail : node.details) {
        if (contains_ci(detail, "quest complete")) return true;
    }
    return false;
}

bool completion_reward_event(const QuestEvent& event) {
    return event.kind == QuestEventKind::Reward ||
           event.kind == QuestEventKind::Morality ||
           event.kind == QuestEventKind::InventoryAdd;
}

void attach_completion_rewards(Graph& graph, const Graph& technical,
                               const ReferenceCatalog& references) {
    std::vector<const QuestEvent*> technical_completions;
    std::vector<const QuestEvent*> scripted_rewards;
    for (const GraphNode& source : technical.nodes) {
        for (const QuestEvent& event : source.events) {
            if (event.kind == QuestEventKind::QuestComplete) {
                technical_completions.push_back(&event);
            } else if (completion_reward_event(event)) {
                scripted_rewards.push_back(&event);
            }
        }
    }
    const std::size_t completion_nodes = std::count_if(
        graph.nodes.begin(), graph.nodes.end(), is_completion_node);
    for (GraphNode& node : graph.nodes) {
        if (!is_completion_node(node)) continue;
        for (const QuestRewardReference& reward : references.quest_rewards) {
            QuestEvent event;
            event.kind = QuestEventKind::Reward;
            event.title = reward.label;
            event.item = reward.label;
            event.amount = reward.amount;
            event.properties.push_back("Quest record reward");
            node.events.push_back(std::move(event));
        }

        std::vector<const QuestEvent*> sources;
        for (const QuestEvent& event : node.events) {
            if (event.kind == QuestEventKind::QuestComplete) {
                sources.push_back(&event);
            }
        }
        if (sources.empty() && completion_nodes == 1) {
            sources = technical_completions;
        }
        for (const QuestEvent* source : sources) {
            for (const QuestEvent* reward : scripted_rewards) {
                if (source->source_class != reward->source_class ||
                    source->source_method != reward->source_method ||
                    source->source_method.empty()) {
                    continue;
                }
                const std::size_t distance =
                    source->source_line < reward->source_line
                        ? reward->source_line - source->source_line
                        : source->source_line - reward->source_line;
                if (distance <= 100) node.events.push_back(*reward);
            }
        }
    }
}

NodeKind node_kind(BeatKind kind) {
    switch (kind) {
        case BeatKind::Dialogue: return NodeKind::Thread;
        case BeatKind::ActorAction: return NodeKind::Action;
        case BeatKind::Camera: return NodeKind::Action;
        case BeatKind::Objective: return NodeKind::Function;
        case BeatKind::Decision: return NodeKind::State;
        case BeatKind::Interaction:
        case BeatKind::Inventory:
        case BeatKind::Timer:
        case BeatKind::WorldState:
        case BeatKind::Task:
        case BeatKind::Reward:
        case BeatKind::Ending: return NodeKind::Action;
    }
    return NodeKind::Action;
}

const char* badge(BeatKind kind) {
    switch (kind) {
        case BeatKind::Dialogue: return "Dialogue";
        case BeatKind::ActorAction: return "Actor action";
        case BeatKind::Camera: return "Camera event";
        case BeatKind::Objective: return "Objective";
        case BeatKind::Decision: return "Condition";
        case BeatKind::Interaction: return "Interaction";
        case BeatKind::Inventory: return "Quest item";
        case BeatKind::Timer: return "Timer";
        case BeatKind::WorldState: return "World event";
        case BeatKind::Task: return "Quest step";
        case BeatKind::Reward: return "Reward";
        case BeatKind::Ending: return "Quest end";
    }
    return "Quest step";
}

std::vector<Beat> story_beats(const GraphNode& source,
                              std::unordered_set<std::string>& seen_dialogue,
                              std::unordered_set<std::string>& seen_events) {
    std::vector<Beat> result;
    std::string participants;
    std::string cutscene;
    std::vector<std::string> source_context;
    int last_beat = -1;
    const QuestEvent* current_event = nullptr;
    for (const std::string& raw : source.details) {
        if (const auto index = numbered_action_index(raw)) {
            current_event = *index < source.events.size()
                ? &source.events[*index] : nullptr;
        }
        const std::string line = strip_number(raw);
        if (line.empty()) continue;
        if (starts_ci(line, "Participants/NPCs: ")) {
            participants = clean_participants(line.substr(
                std::string("Participants/NPCs: ").size()));
            continue;
        }
        if (starts_ci(line, "Cutscene ID: ")) {
            cutscene = trim(line.substr(std::string("Cutscene ID: ").size()));
            if (last_beat >= 0 &&
                starts_ci(result[size_t(last_beat)].title, "Cutscene: ")) {
                result[size_t(last_beat)].metadata.push_back(
                    "Cutscene: " + cutscene);
            }
            continue;
        }
        if (starts_ci(line, "Related audio: ")) {
            if (last_beat >= 0) result[size_t(last_beat)].metadata.push_back(line);
            continue;
        }
        if (starts_ci(line, "Item ID: ")) {
            if (last_beat >= 0) result[size_t(last_beat)].metadata.push_back(line);
            continue;
        }
        if (starts_ci(line, "Animation ID:") ||
            starts_ci(line, "Second animation ID:")) {
            if (last_beat >= 0 &&
                result[size_t(last_beat)].kind == BeatKind::ActorAction) {
                result[size_t(last_beat)].metadata.push_back(line);
            }
            continue;
        }
        if (starts_ci(line, "Actor detail: ")) {
            if (last_beat >= 0 &&
                result[size_t(last_beat)].kind == BeatKind::ActorAction) {
                result[size_t(last_beat)].details.push_back(clean_names(
                    line.substr(std::string("Actor detail: ").size())));
            }
            continue;
        }
        if ((starts_ci(line, "Camera position: ") ||
             starts_ci(line, "Camera focus: ") ||
             starts_ci(line, "Field of view: ") ||
             starts_ci(line, "The look-at prompt ")) &&
            last_beat >= 0 &&
            result[size_t(last_beat)].kind == BeatKind::Camera) {
            result[size_t(last_beat)].details.push_back(clean_names(line));
            continue;
        }
        if (starts_ci(line, "Dialogue ID: ") ||
            starts_ci(line, "Objective ID: ") ||
            starts_ci(line, "Source:") ||
            starts_ci(line, "Source class:") ||
            starts_ci(line, "Layer ID:") ||
            starts_ci(line, "Movie asset:") ||
            starts_ci(line, "Entity definition:") ||
            starts_ci(line, "NPCs/entities:") ||
            starts_ci(line, "Markers:")) {
            continue;
        }
        if (starts_ci(line, "Areas/levels: ")) {
            source_context.push_back("Location: " + clean_names(line.substr(
                std::string("Areas/levels: ").size())));
            continue;
        }
        if (starts_ci(line, "Coordinates: ")) {
            source_context.push_back(line);
            continue;
        }

        std::string tag, speaker, text;
        if (parse_dialogue(line, tag, speaker, text)) {
            const std::string key = lower_ascii(
                (cutscene.empty() ? std::to_string(source.id) : cutscene) +
                "|" + tag);
            if (!seen_dialogue.insert(key).second) continue;
            Beat beat;
            beat.kind = BeatKind::Dialogue;
            beat.title = speaker.empty() ? text : speaker + ": " + text;
            beat.subtitle = speaker.empty() && !participants.empty()
                ? participants : std::string();
            beat.metadata.push_back("Dialogue ID: " + tag);
            if (current_event) beat.events.push_back(*current_event);
            if (!cutscene.empty()) {
                beat.metadata.push_back("Cutscene: " + cutscene);
            }
            result.push_back(std::move(beat));
            last_beat = int(result.size()) - 1;
            continue;
        }

        Beat beat;
        if (parse_direct_dialogue(line, beat) || classify_action(line, beat) ||
            (current_event && classify_event(*current_event, line, beat))) {
            if (current_event) beat.events.push_back(*current_event);
            const bool event = starts_ci(beat.title, "Cutscene: ") ||
                               starts_ci(beat.title, "Cinematic: ");
            if (event && !seen_events.insert(lower_ascii(beat.title)).second) {
                last_beat = -1;
                continue;
            }
            bool duplicate = false;
            if (!result.empty() && lower_ascii(result.back().title) ==
                                      lower_ascii(beat.title)) {
                duplicate = true;
            }
            if (!duplicate) {
                result.push_back(std::move(beat));
                last_beat = int(result.size()) - 1;
            }
            continue;
        }
        if ((starts_ci(line, "Position: ") ||
             starts_ci(line, "Spawn position: ") ||
             starts_ci(line, "Marker ID: ") ||
             starts_ci(line, "Level: ") ||
             starts_ci(line, "World placement: ") ||
             starts_ci(line, "Areas/levels: ")) && last_beat >= 0) {
            result[size_t(last_beat)].details.push_back(clean_names(line));
        }
    }
    if (!result.empty() && !source_context.empty()) {
        std::size_t target = 0;
        while (target < result.size() &&
               result[target].kind == BeatKind::Dialogue) ++target;
        if (target == result.size()) target = 0;
        for (const std::string& context : source_context) {
            if (std::find(result[target].details.begin(),
                          result[target].details.end(), context) ==
                result[target].details.end()) {
                result[target].details.push_back(context);
            }
        }
    }
    return result;
}

std::string metadata_value(const Beat& beat, const std::string& prefix) {
    for (const std::string& value : beat.metadata) {
        if (starts_ci(value, prefix)) return trim(value.substr(prefix.size()));
    }
    return {};
}

std::string quoted_dialogue(const std::string& value) {
    const std::size_t colon = value.find(':');
    if (colon == std::string::npos) return "\"" + value + "\"";
    const std::string speaker = trim(value.substr(0, colon));
    std::string line = trim(value.substr(colon + 1));
    std::replace(line.begin(), line.end(), '"', '\'');
    return speaker + ": \"" + line + "\"";
}

GraphNode compose_story_step(const GraphNode& source,
                             const std::vector<Beat>& beats,
                             std::size_t& dialogue_count,
                             const std::string& forced_title = {}) {
    GraphNode node;
    node.x = source.x;
    node.y = source.y;

    const Beat* primary = nullptr;
    for (const Beat& beat : beats) {
        if (beat.kind != BeatKind::Dialogue &&
            !starts_ci(beat.title, "Cutscene: ") &&
            !starts_ci(beat.title, "Cinematic: ")) {
            primary = &beat;
            break;
        }
    }
    if (!primary) {
        for (const Beat& beat : beats) {
            if (beat.kind != BeatKind::Dialogue) {
                primary = &beat;
                break;
            }
        }
    }

    if (primary) {
        node.kind = node_kind(primary->kind);
        node.badge = badge(primary->kind);
        node.title = primary->title;
        node.subtitle = primary->subtitle;
    } else {
        node.kind = NodeKind::Thread;
        node.badge = "Dialogue";

        std::vector<std::string> speakers;
        std::string scene;
        for (const Beat& beat : beats) {
            if (scene.empty()) scene = metadata_value(beat, "Cutscene: ");
            const std::size_t colon = beat.title.find(':');
            if (colon == std::string::npos) continue;
            const std::string speaker = trim(beat.title.substr(0, colon));
            if (speaker.empty() ||
                std::find(speakers.begin(), speakers.end(), speaker) !=
                    speakers.end()) {
                continue;
            }
            speakers.push_back(speaker);
        }
        if (!speakers.empty()) {
            node.title = "Conversation: ";
            const std::size_t shown = std::min<std::size_t>(3, speakers.size());
            for (std::size_t i = 0; i < shown; ++i) {
                if (i != 0) node.title += " / ";
                node.title += speakers[i];
            }
            if (speakers.size() > shown) node.title += " / others";
        } else if (!scene.empty()) {
            node.title = "Conversation: " + clean_names(scene);
        } else {
            node.title = "Conversation";
        }
        node.subtitle = scene.empty()
            ? "Dialogue scene"
            : "Scene: " + clean_names(scene);
    }
    if (!forced_title.empty()) {
        node.title = forced_title;
        node.subtitle.clear();
    }

    bool in_dialogue = false;
    std::set<std::string> metadata_seen;
    for (const Beat& beat : beats) {
        for (const std::string& value : beat.metadata) {
            if (metadata_seen.insert(value).second) node.metadata.push_back(value);
        }

        if (beat.kind == BeatKind::Dialogue) {
            ++dialogue_count;
            if (!in_dialogue) {
                node.details.push_back("Dialogue:");
                in_dialogue = true;
            }
            node.details.push_back("  " + quoted_dialogue(beat.title));
            for (const std::string& detail : beat.details) {
                node.details.push_back("  " + detail);
            }
            continue;
        }

        in_dialogue = false;
        if (&beat == primary && forced_title.empty()) {
            node.details.insert(node.details.end(), beat.details.begin(),
                                beat.details.end());
            continue;
        }
        if (beat.kind == BeatKind::Decision) {
            node.details.push_back("Progression: " + beat.title);
        } else {
            node.details.push_back(beat.title);
        }
        for (const std::string& detail : beat.details) {
            node.details.push_back("  " + detail);
        }
    }
    return node;
}

float estimated_node_height(const GraphNode& node) {
    float lines = 3.5f;
    for (const std::string& detail : node.details) {
        lines += float(std::max<std::size_t>(1, (detail.size() + 54) / 55));
    }
    return lines * 22.0f + 30.0f;
}

void add_link(Graph& graph,
              std::set<std::tuple<int, int, std::string>>& unique,
              int from, int to, std::string label) {
    if (from <= 0 || to <= 0) return;
    const auto key = std::make_tuple(from, to, label);
    if (!unique.insert(key).second) return;
    GraphLink link;
    link.id = int(graph.links.size()) + 1;
    link.from_node = from;
    link.to_node = to;
    link.label = std::move(label);
    graph.links.push_back(std::move(link));
}

const GraphNode* find_node(const Graph& graph, int id) {
    for (const GraphNode& node : graph.nodes) {
        if (node.id == id) return &node;
    }
    return nullptr;
}

bool is_decision(const Graph& graph, int id) {
    const GraphNode* node = find_node(graph, id);
    return node && node->badge == "Condition";
}

void layout_branching_story(Graph& graph, int root_story,
                            std::vector<int>& layout_order) {
    std::unordered_map<int, GraphNode*> nodes;
    std::unordered_map<int, std::vector<int>> forward;
    std::unordered_map<int, int> indegree;
    for (GraphNode& node : graph.nodes) {
        nodes[node.id] = &node;
        indegree[node.id] = 0;
    }
    for (const GraphLink& link : graph.links) {
        if (!nodes.count(link.from_node) || !nodes.count(link.to_node) ||
            link.from_node == link.to_node ||
            lower_ascii(link.label) == "no") {
            continue;
        }
        std::vector<int>& targets = forward[link.from_node];
        if (std::find(targets.begin(), targets.end(), link.to_node) ==
            targets.end()) {
            targets.push_back(link.to_node);
            ++indegree[link.to_node];
        }
    }
    for (auto& entry : forward) {
        std::sort(entry.second.begin(), entry.second.end());
        if (entry.second.size() < 2) continue;
        const bool decision = is_decision(graph, entry.first);
        for (std::size_t i = 0; i < entry.second.size(); ++i) {
            for (GraphLink& link : graph.links) {
                if (link.from_node != entry.first ||
                    link.to_node != entry.second[i] || !link.label.empty()) {
                    continue;
                }
                if (decision && entry.second.size() == 2) {
                    link.label = i == 0 ? "Yes" : "No";
                } else {
                    link.label = "Path " + std::to_string(i + 1);
                }
            }
        }
    }

    std::unordered_map<int, int> remaining = indegree;
    std::unordered_map<int, int> depth;
    std::set<int> ready;
    for (const auto& entry : nodes) {
        depth[entry.first] = 0;
        if (remaining[entry.first] == 0) ready.insert(entry.first);
    }
    std::vector<int> topological;
    while (!ready.empty()) {
        const int current = *ready.begin();
        ready.erase(ready.begin());
        topological.push_back(current);
        for (int target : forward[current]) {
            depth[target] = std::max(depth[target], depth[current] + 1);
            if (--remaining[target] == 0) ready.insert(target);
        }
    }
    if (topological.size() != nodes.size()) {
        std::vector<int> unresolved;
        for (const auto& entry : nodes) {
            if (std::find(topological.begin(), topological.end(),
                          entry.first) == topological.end()) {
                unresolved.push_back(entry.first);
            }
        }
        std::sort(unresolved.begin(), unresolved.end());
        int fallback_depth = 0;
        for (const auto& entry : depth) {
            fallback_depth = std::max(fallback_depth, entry.second);
        }
        for (int id : unresolved) {
            depth[id] = ++fallback_depth;
            topological.push_back(id);
        }
    }

    std::unordered_set<int> reachable;
    if (root_story > 0 && nodes.count(root_story)) {
        std::deque<int> queue{root_story};
        while (!queue.empty()) {
            const int current = queue.front();
            queue.pop_front();
            if (!reachable.insert(current).second) continue;
            for (int target : forward[current]) queue.push_back(target);
        }
    }
    int max_reachable_depth = 0;
    for (int id : reachable) {
        max_reachable_depth = std::max(max_reachable_depth, depth[id]);
    }
    std::vector<int> disconnected;
    for (const auto& entry : nodes) {
        if (!reachable.count(entry.first)) disconnected.push_back(entry.first);
    }
    std::sort(disconnected.begin(), disconnected.end());
    for (int id : disconnected) depth[id] = ++max_reachable_depth;

    int max_depth = 0;
    for (const auto& entry : depth) max_depth = std::max(max_depth, entry.second);
    std::vector<float> level_height(size_t(max_depth + 1), 0.0f);
    for (const auto& entry : nodes) {
        level_height[size_t(depth[entry.first])] = std::max(
            level_height[size_t(depth[entry.first])],
            estimated_node_height(*entry.second));
    }
    std::vector<float> level_y(size_t(max_depth + 1), 0.0f);
    for (int level = 1; level <= max_depth; ++level) {
        level_y[size_t(level)] = level_y[size_t(level - 1)] +
            level_height[size_t(level - 1)] + 150.0f;
    }

    std::unordered_map<int, float> lane_x;
    for (const auto& entry : nodes) lane_x[entry.first] = 0.0f;
    std::sort(topological.begin(), topological.end(),
              [&](int a, int b) {
                  if (depth[a] != depth[b]) return depth[a] < depth[b];
                  return a < b;
              });
    constexpr float kBranchSpacing = 760.0f;
    for (int source : topological) {
        const std::vector<int>& targets = forward[source];
        if (targets.size() < 2) continue;
        const float centre = (float(targets.size()) - 1.0f) * 0.5f;
        for (std::size_t i = 0; i < targets.size(); ++i) {
            const float branch_x = lane_x[source] +
                (float(i) - centre) * kBranchSpacing;
            int current = targets[i];
            lane_x[current] = branch_x;
            while (forward[current].size() == 1) {
                const int next = forward[current].front();
                if (indegree[next] > 1) break;
                lane_x[next] = branch_x;
                current = next;
            }
        }
    }

    layout_order.clear();
    for (int id : topological) {
        GraphNode* node = nodes[id];
        node->x = lane_x[id];
        node->y = level_y[size_t(depth[id])];
        if (id != root_story) layout_order.push_back(id);
    }
}

std::vector<const GraphNode*> primary_entity_sequence(const Graph& technical) {
    std::vector<const GraphNode*> best;
    std::size_t best_dialogue = 0;
    for (std::size_t i = 0; i < technical.nodes.size(); ++i) {
        const GraphNode& header = technical.nodes[i];
        const std::string suffix = " - behaviour";
        if (header.kind != NodeKind::Thread ||
            header.title.size() <= suffix.size() ||
            !contains_ci(header.title, suffix)) {
            continue;
        }
        const std::string actor = header.title.substr(
            0, header.title.size() - suffix.size());
        const std::string phase_prefix = actor + " - Phase ";
        std::vector<const GraphNode*> candidate;
        std::size_t dialogue = 0;
        for (std::size_t j = i + 1; j < technical.nodes.size(); ++j) {
            const GraphNode& node = technical.nodes[j];
            if (node.kind != NodeKind::State ||
                !starts_ci(node.title, phase_prefix)) {
                break;
            }
            candidate.push_back(&node);
            for (const std::string& detail : node.details) {
                if (starts_ci(strip_number(detail), "Dialogue [")) ++dialogue;
            }
        }
        if (dialogue > best_dialogue ||
            (dialogue == best_dialogue && candidate.size() > best.size())) {
            best_dialogue = dialogue;
            best = std::move(candidate);
        }
    }
    return best;
}

const GraphNode* source_with_detail(const Graph& graph,
                                    const std::string& fragment) {
    for (const GraphNode& node : graph.nodes) {
        for (const std::string& detail : node.details) {
            if (contains_ci(detail, fragment)) return &node;
        }
    }
    return nullptr;
}

int childhood_job_for_event(const std::string& title, int current) {
    const std::string value = lower_ascii(title);
    if (value.find("rose gold") != std::string::npos ||
        value.find("rose idle") != std::string::npos ||
        value.find("rose smash") != std::string::npos) return 6;
    if (value.find("barnum") != std::string::npos ||
        value.find("into pose") != std::string::npos ||
        value.find("out of pose") != std::string::npos ||
        value.find("release nasty") != std::string::npos) return 0;
    if (value.find("balthazar") != std::string::npos ||
        value.find("beetle") != std::string::npos) return 1;
    if (value.find("rex") != std::string::npos ||
        value.find("rose dog") != std::string::npos ||
        value.find("rose get up") != std::string::npos) return 5;
    if (value.find("monty") != std::string::npos ||
        value.find("deidre") != std::string::npos ||
        value.find("belinda") != std::string::npos ||
        value.find("house") != std::string::npos ||
        value.find("move on") != std::string::npos) return 2;
    if (value.find("derek") != std::string::npos ||
        value.find("warrant") != std::string::npos ||
        value.find("arfur") != std::string::npos ||
        value.find("searching interact") != std::string::npos ||
        value.find("end hero near alley") != std::string::npos ||
        value.find("rose start") != std::string::npos ||
        value.find("spot dog") != std::string::npos) return 3;
    if (value.find("betty") != std::string::npos ||
        value.find("pete") != std::string::npos ||
        value.find("magpie") != std::string::npos ||
        value.find("bottle") != std::string::npos ||
        value.find("booze") != std::string::npos ||
        value.find("accept rose") != std::string::npos) return 4;
    return current;
}

void mark_childhood_alternative(Beat& beat) {
    if (!starts_ci(beat.title, "Cutscene: ")) return;
    const std::string value = lower_ascii(beat.title);
    static const char* variants[] = {
        " nice", " evil", "targeted", "not targeted",
        "complete derek", "complete arfur", "complete betty",
        "complete pete", "wait downstairs", "wait upstairs",
        "release nasty",
    };
    bool alternative = false;
    for (const char* variant : variants) {
        if (value.find(variant) != std::string::npos) {
            alternative = true;
            break;
        }
    }
    if (alternative) {
        beat.title = "Alternative scene: " + beat.title.substr(
            std::string("Cutscene: ").size());
    }
}

std::string readable_childhood_scene(std::string title) {
    bool alternative = false;
    if (starts_ci(title, "Alternative scene: ")) {
        alternative = true;
        title = trim(title.substr(std::string("Alternative scene: ").size()));
    } else if (starts_ci(title, "Cutscene: ")) {
        title = trim(title.substr(std::string("Cutscene: ").size()));
    }
    const std::string key = lower_ascii(title);
    static const std::unordered_map<std::string, std::string> names = {
        {"set rose mode", "Rose warms herself by the fire"},
        {"rose poo", "Rose reacts after bird droppings land on Sparrow"},
        {"rose poo 3", "Rose talks with Sparrow about Castle Fairfax"},
        {"rose look square", "Rose notices activity in the town square"},
        {"arfur offer", "Arfur approaches Rose and Sparrow"},
        {"rose creep", "Rose reacts to Arfur"},
        {"rose beckons to crowd", "Rose calls Sparrow toward the crowd"},
        {"rose looking over crowd", "Rose tries to see over the crowd"},
        {"rose arrived at murgo", "Rose and Sparrow reach Murgo's crowd"},
        {"murgo pitch 1", "Murgo presents the music box to the crowd"},
        {"theresa rose", "Theresa tells Rose the music box may be real"},
        {"theresa rose 2", "Rose decides to earn five gold"},
        {"rose get money", "Rose and Sparrow look for paid work"},
        {"rose enough money", "Rose tells Sparrow they have enough gold"},
        {"rose enough money short", "Rose reminds Sparrow to return to Murgo"},
        {"murgo buy", "Rose and Sparrow buy the music box from Murgo"},
        {"rose wish sequence", "Rose uses the music box and makes her wish"},
        {"rose wish sequence 2", "The music box disappears"},
        {"rose dog bed", "The dog follows the children back to their shack"},
        {"rose go to bed", "Rose waits for Sparrow to go to bed"},
        {"guard morning", "Lucien's guard arrives at the shack"},
        {"rose morning", "Rose wakes Sparrow and prepares to leave"},
        {"rose morning 2", "Rose promises to return for the dog"},
        {"jeeves greet", "Jeeves receives the children at Fairfax Castle"},
        {"jeeves to study", "Jeeves leads the children to Lucien's study"},
        {"lucien intro 2", "Lucien questions Rose about the music box"},
        {"rose reacts to magic", "The magic circle reacts to Rose"},
        {"rose circle prompt", "Lucien asks Sparrow to enter the circle"},
        {"lucien circle", "Lucien discovers the children's Hero blood"},
        {"barnum approach", "Barnum offers a paid portrait job"},
        {"barnum accept", "Sparrow accepts Barnum's portrait job"},
        {"rose into pose", "Rose gets ready for Barnum's portrait"},
        {"barnum no expression", "Barnum waits for Sparrow to pose"},
        {"rose out of pose", "The portrait pose ends"},
        {"barnum release nasty", "Barnum reacts to Sparrow's pose"},
        {"rose complete", "Rose reacts after Barnum pays for the portrait"},
        {"rose complete 2", "Rose reflects on the portrait job"},
        {"balthazar approach", "Balthazar offers gold to clear his warehouse"},
        {"rex rose", "Rex attacks Rose"},
        {"rose get up", "Rose gets back up after Rex's attack"},
        {"rose dog", "Rose comforts the injured dog"},
        {"monty intro", "Monty asks the children to deliver a love letter"},
        {"rose read letter", "Rose reads Monty's love letter"},
        {"rose near house", "The children reach Belinda's house"},
        {"rose wait door", "Rose waits for someone to answer the door"},
        {"deidre open door", "Deidre answers the door"},
        {"rose deidre got money", "Deidre offers gold for Monty's letter"},
        {"rose enter house", "The children decide who receives the letter"},
        {"rose wait house", "Rose waits while Sparrow chooses who gets the letter"},
        {"rose approach belinda", "The children find Belinda upstairs"},
        {"rose wait downstairs", "Sparrow takes the letter back downstairs"},
        {"rose wait upstairs", "Sparrow takes the letter upstairs to Belinda"},
        {"rose wait downstairs end", "Sparrow gives the letter to Deidre"},
        {"rose move on 2", "Rose reacts to Sparrow's choice"},
        {"rose end hero near alley", "Rose points out the alley where the warrants landed"},
        {"rose searching interact", "Rose reminds Sparrow to search for warrants"},
        {"derek call over", "Derek calls the children over"},
        {"derek approach", "Derek asks the children to find five warrants"},
        {"derek accept", "Sparrow accepts Derek's warrant job"},
        {"rose start", "Rose begins searching for the warrants"},
        {"rose start 2", "The warrant search begins"},
        {"rose spot dog", "Rose notices the dog again"},
        {"rose got dog warrant", "The dog finds a warrant for Sparrow"},
        {"arfur confront", "Arfur offers to buy the warrants"},
        {"arfur targeted 1shot", "Rose recalls standing up to Arfur"},
        {"arfur targeted 1help", "Rose recalls helping Arfur"},
        {"arfur targeted 1", "Rose urges Sparrow to refuse Arfur"},
        {"arfur targeted 2", "Rose weighs accepting Arfur's gold"},
        {"arfur targeted 3", "Sparrow confronts Arfur"},
        {"arfur not targeted 1", "Arfur waits for Sparrow's answer"},
        {"arfur not targeted 2", "Rose urges Sparrow to decide"},
        {"arfur walk away", "Arfur stops Sparrow from leaving"},
        {"rose got warrants", "Rose confirms that all five warrants were found"},
        {"rose spot stuck warrant", "Rose spots a warrant caught nearby"},
        {"rose complete derek", "Sparrow returns the warrants to Derek"},
        {"rose complete arfur", "Sparrow sells the warrants to Arfur"},
        {"rose complete warrant", "Rose tells Sparrow to keep searching"},
        {"rose spot warrant", "Rose spots another warrant"},
        {"rose find warrant first", "Sparrow finds the first warrant"},
        {"rose find warrant second", "Sparrow finds the second warrant"},
        {"rose find warrant third", "Sparrow finds the third warrant"},
        {"rose find warrant forth", "Sparrow finds the fourth warrant"},
        {"betty approach", "Pete and Betty offer opposing bottle jobs"},
        {"betty approach booze", "Pete and Betty each offer gold for the bottle"},
        {"accept rose", "Rose accepts the bottle search"},
        {"rose spot magpie", "Rose finds Magpie and the stolen bottle"},
        {"rose wait magpie", "Rose waits while Sparrow sneaks toward the bottle"},
        {"rose fail", "Magpie wakes before Sparrow reaches the bottle"},
        {"rose fail 2", "Magpie falls asleep and Sparrow can try again"},
        {"rose got bottle", "Sparrow recovers the stolen bottle"},
        {"rose complete betty", "Sparrow gives the bottle to Betty"},
        {"rose complete pete", "Sparrow gives the bottle to Pete"},
        {"rose smash", "Rose reacts while Sparrow searches for paid work"},
        {"rose idle interact", "Rose checks whether Sparrow is ready to continue"},
        {"rose idle", "Rose waits while Sparrow searches for paid work"},
        {"rose gold first nice", "Rose reacts to the first gold coin after a good choice"},
        {"rose gold first evil", "Rose reacts to the first gold coin after an evil choice"},
        {"rose gold second nice", "Rose reacts to the second gold coin after a good choice"},
        {"rose gold second evil", "Rose reacts to the second gold coin after an evil choice"},
        {"rose gold third nice", "Rose reacts to the third gold coin after a good choice"},
        {"rose gold third evil", "Rose reacts to the third gold coin after an evil choice"},
        {"rose gold forth nice", "Rose reacts to the fourth gold coin after a good choice"},
        {"rose gold forth evil", "Rose reacts to the fourth gold coin after an evil choice"},
        {"rose gold fifth nice", "Rose reacts to the fifth gold coin after a good choice"},
        {"rose gold fifth evil", "Rose reacts to the fifth gold coin after an evil choice"},
    };
    auto found = names.find(key);
    std::string readable = found == names.end()
        ? clean_names(title) : found->second;
    if (readable.empty()) return {};
    return alternative ? "Alternative: " + readable : readable;
}

std::string dialogue_node_title(const std::vector<Beat>& beats,
                                std::size_t begin, std::size_t end) {
    std::vector<std::string> speakers;
    for (std::size_t i = begin; i < end; ++i) {
        const std::size_t colon = beats[i].title.find(':');
        if (colon == std::string::npos) continue;
        const std::string speaker = trim(beats[i].title.substr(0, colon));
        if (!speaker.empty() &&
            std::find(speakers.begin(), speakers.end(), speaker) ==
                speakers.end()) {
            speakers.push_back(speaker);
        }
    }
    if (speakers.empty()) return "Dialogue";
    std::string title = "Dialogue: " + speakers.front();
    if (speakers.size() == 2) title += " and " + speakers[1];
    else if (speakers.size() > 2) title += ", " + speakers[1] + " and others";
    return title;
}

struct GenderDialogue {
    std::string speaker;
    std::string subject;
    std::string male;
    std::string female;
};

std::optional<GenderDialogue> gender_dialogue(const Beat& beat) {
    static const std::regex variants(
        R"variant(^\s*([^:]+):\s*Male (Sparrow|Hero):\s*"([^"]*)"\s*/\s*Female (Sparrow|Hero):\s*"([^"]*)"\s*$)variant",
        std::regex::icase);
    std::smatch match;
    if (!std::regex_match(beat.title, match, variants)) return std::nullopt;
    return GenderDialogue{
        clean_names(match[1].str()), clean_names(match[2].str()),
        match[3].str(), match[5].str()};
}

std::vector<std::string> gender_metadata(
    const Beat& beat, const std::string& suffix) {
    std::vector<std::string> result;
    const std::string lower_suffix = lower_ascii(suffix);
    for (std::string metadata : beat.metadata) {
        if (starts_ci(metadata, "Dialogue ID: ")) {
            metadata += suffix;
            result.push_back(std::move(metadata));
            continue;
        }
        if (starts_ci(metadata, "Related audio: ")) {
            const std::string lower = lower_ascii(metadata);
            const std::string folder = "\\" + lower_suffix.substr(1) + "\\";
            if (lower.find(lower_suffix + ".wav") == std::string::npos &&
                lower.find(folder) == std::string::npos) {
                continue;
            }
        }
        result.push_back(std::move(metadata));
    }
    return result;
}

int append_story_beat_nodes(
    Graph& graph, const std::vector<Beat>& beats, int previous,
    float x, float& y, std::size_t& dialogue_count,
    std::set<std::tuple<int, int, std::string>>& unique_links,
    const std::string& first_link_label = {},
    std::vector<int>* terminal_nodes = nullptr) {
    bool first_node = true;
    std::vector<int> pending_branch_ends;
    std::string current_scene;
    std::vector<std::string> pending_scene_details;
    std::vector<std::string> pending_scene_metadata;
    std::vector<QuestEvent> pending_scene_events;
    auto link_to = [&](int target) {
        if (!pending_branch_ends.empty()) {
            for (int branch_end : pending_branch_ends) {
                add_link(graph, unique_links, branch_end, target, "");
            }
            pending_branch_ends.clear();
            return;
        }
        add_link(graph, unique_links, previous, target,
                 first_node ? first_link_label
                            : is_decision(graph, previous)
                                ? "Yes" : std::string());
    };
    for (std::size_t i = 0; i < beats.size();) {
        if (beats[i].source_boundary) {
            current_scene.clear();
            pending_scene_details.clear();
            pending_scene_metadata.clear();
            pending_scene_events.clear();
            ++i;
            continue;
        }
        const bool scene_context =
            starts_ci(beats[i].title, "Cutscene: ") ||
            starts_ci(beats[i].title, "Alternative scene: ");
        const bool has_scene_content = i + 1 < beats.size() &&
            (beats[i + 1].kind == BeatKind::ActorAction ||
             beats[i + 1].kind == BeatKind::Dialogue);
        if (scene_context && has_scene_content) {
            current_scene = readable_childhood_scene(beats[i].title);
            pending_scene_details = beats[i].details;
            pending_scene_metadata = beats[i].metadata;
            pending_scene_events = beats[i].events;
            ++i;
            continue;
        }

        if (beats[i].kind == BeatKind::Dialogue) {
            std::size_t end = i + 1;
            while (end < beats.size() &&
                   beats[end].kind == BeatKind::Dialogue) ++end;
            std::size_t gender_line = end;
            for (std::size_t line = i; line < end; ++line) {
                if (gender_dialogue(beats[line])) {
                    gender_line = line;
                    break;
                }
            }
            if (gender_line > i && gender_line < end) {
                end = gender_line;
            } else if (gender_line == i) {
                const GenderDialogue variants = *gender_dialogue(beats[i]);
                if (previous <= 0 && pending_branch_ends.empty()) {
                    GraphNode fork;
                    fork.id = int(graph.nodes.size()) + 1;
                    fork.kind = NodeKind::State;
                    fork.badge = "Player gender";
                    fork.title = variants.subject +
                        "'s gender determines this dialogue";
                    fork.x = x;
                    fork.y = y;
                    y += estimated_node_height(fork) + 150.0f;
                    graph.nodes.push_back(std::move(fork));
                    previous = graph.nodes.back().id;
                    first_node = false;
                }
                std::vector<int> branch_sources = pending_branch_ends;
                pending_branch_ends.clear();
                if (branch_sources.empty() && previous > 0) {
                    branch_sources.push_back(previous);
                }
                auto make_branch = [&](const char* title,
                                       const std::string& text,
                                       const std::string& suffix,
                                       float branch_x) {
                    GraphNode branch;
                    branch.id = int(graph.nodes.size()) + 1;
                    branch.kind = NodeKind::Thread;
                    branch.badge = "Dialogue branch";
                    branch.title = title;
                    branch.subtitle = variants.speaker;
                    branch.details.push_back(
                        variants.speaker + ": \"" + text + "\"");
                    if (!current_scene.empty()) {
                        branch.details.push_back("Scene: " + current_scene);
                    }
                    branch.details.insert(branch.details.end(),
                                          pending_scene_details.begin(),
                                          pending_scene_details.end());
                    branch.metadata = gender_metadata(beats[i], suffix);
                    branch.metadata.insert(branch.metadata.begin(),
                                           pending_scene_metadata.begin(),
                                           pending_scene_metadata.end());
                    branch.events = beats[i].events;
                    branch.events.insert(branch.events.begin(),
                                         pending_scene_events.begin(),
                                         pending_scene_events.end());
                    branch.x = branch_x;
                    branch.y = y;
                    graph.nodes.push_back(std::move(branch));
                    return graph.nodes.back().id;
                };

                const int male = make_branch(
                    "If male", variants.male, "_HM", x - 380.0f);
                const float male_height = estimated_node_height(
                    graph.nodes[size_t(male - 1)]);
                const int female = make_branch(
                    "If female", variants.female, "_HF", x + 380.0f);
                const float female_height = estimated_node_height(
                    graph.nodes[size_t(female - 1)]);
                for (int branch_source : branch_sources) {
                    add_link(graph, unique_links, branch_source, male,
                             "Male " + variants.subject);
                    add_link(graph, unique_links, branch_source, female,
                             "Female " + variants.subject);
                }

                y += std::max(male_height, female_height) + 150.0f;
                pending_branch_ends = {male, female};
                previous = 0;
                first_node = false;
                dialogue_count += 2;
                current_scene.clear();
                pending_scene_details.clear();
                pending_scene_metadata.clear();
                pending_scene_events.clear();
                ++i;
                continue;
            }
            GraphNode node;
            node.id = int(graph.nodes.size()) + 1;
            node.kind = NodeKind::Thread;
            node.badge = "Dialogue";
            node.title = dialogue_node_title(beats, i, end);
            if (!current_scene.empty()) {
                node.subtitle = "Scene: " + current_scene;
            }
            std::set<std::string> metadata_seen;
            for (const std::string& metadata : pending_scene_metadata) {
                if (metadata_seen.insert(metadata).second) {
                    node.metadata.push_back(metadata);
                }
            }
            node.events = pending_scene_events;
            for (std::size_t line = i; line < end; ++line) {
                ++dialogue_count;
                node.details.push_back(quoted_dialogue(beats[line].title));
                for (const std::string& detail : beats[line].details) {
                    node.details.push_back(detail);
                }
                for (const std::string& metadata : beats[line].metadata) {
                    if (metadata_seen.insert(metadata).second) {
                        node.metadata.push_back(metadata);
                    }
                }
                node.events.insert(node.events.end(), beats[line].events.begin(),
                                   beats[line].events.end());
            }
            node.details.insert(node.details.end(),
                                pending_scene_details.begin(),
                                pending_scene_details.end());
            pending_scene_details.clear();
            pending_scene_metadata.clear();
            pending_scene_events.clear();
            node.x = x;
            node.y = y;
            y += estimated_node_height(node) + 105.0f;
            graph.nodes.push_back(std::move(node));
            link_to(graph.nodes.back().id);
            previous = graph.nodes.back().id;
            first_node = false;
            i = end;
            continue;
        }

        GraphNode node;
        node.id = int(graph.nodes.size()) + 1;
        node.kind = node_kind(beats[i].kind);
        node.badge = badge(beats[i].kind);
        node.title = beats[i].title;
        node.subtitle = beats[i].subtitle;
        node.details = beats[i].details;
        node.metadata = beats[i].metadata;
        node.events = beats[i].events;

        if (starts_ci(beats[i].title, "Cutscene: ") ||
            starts_ci(beats[i].title, "Alternative scene: ")) {
            current_scene = readable_childhood_scene(beats[i].title);
            node.title = current_scene;
            node.badge = starts_ci(beats[i].title, "Alternative scene: ")
                ? "Alternative scene" : "Story event";
            node.kind = NodeKind::Action;
        } else if (starts_ci(beats[i].title, "Cinematic: ")) {
            current_scene = beats[i].title;
            node.badge = "Cinematic";
        } else if (beats[i].kind == BeatKind::ActorAction) {
            node.badge = "Actor action";
            node.kind = NodeKind::Action;
            if (!current_scene.empty()) {
                node.subtitle = "Scene: " + current_scene;
            }
            node.details.insert(node.details.begin(),
                                pending_scene_details.begin(),
                                pending_scene_details.end());
            node.metadata.insert(node.metadata.begin(),
                                 pending_scene_metadata.begin(),
                                 pending_scene_metadata.end());
            node.events.insert(node.events.begin(), pending_scene_events.begin(),
                               pending_scene_events.end());
            pending_scene_details.clear();
            pending_scene_metadata.clear();
            pending_scene_events.clear();
        } else {
            current_scene.clear();
            pending_scene_details.clear();
            pending_scene_metadata.clear();
            pending_scene_events.clear();
        }
        if (contains_ci(node.title, "Go to Hero Wish Marker")) {
            node.title = "Move Sparrow to the wishing spot";
            node.badge = "Player action";
            node.subtitle.clear();
        }
        if (starts_ci(node.title, "Has the trigger been entered?")) {
            node.title = "Has Sparrow reached the quiet wishing spot?";
            node.badge = "Condition";
            node.subtitle.clear();
        }
        if (contains_ci(node.title,
                        "Quest Manager.Hero Entity inside shack trigger")) {
            node.title = "Has Sparrow entered the shack?";
            node.badge = "Condition";
            node.subtitle.clear();
        }
        if (starts_ci(node.title, "Objective: ")) {
            node.title = trim(node.title.substr(
                std::string("Objective: ").size()));
            node.badge = "Objective";
            node.subtitle.clear();
        }
        if (starts_ci(node.title,
                      "Travel to Albion / Fairfax Castle Gardens")) {
            node.title =
                "Travel from Bowerstone Old Town to Fairfax Castle";
            node.badge = "Travel";
            node.subtitle.clear();
        }
        if (starts_ci(node.title, "Cinematic: Intro")) {
            node.title = "Opening cinematic";
            node.badge = "Cinematic";
            node.metadata.push_back("Movie asset: Intro.bik");
        }

        if (!node.title.empty()) {
            node.x = x;
            node.y = y;
            y += estimated_node_height(node) + 105.0f;
            graph.nodes.push_back(std::move(node));
            link_to(graph.nodes.back().id);
            previous = graph.nodes.back().id;
            first_node = false;
        }
        ++i;
    }
    if (terminal_nodes) {
        *terminal_nodes = pending_branch_ends.empty()
            ? (previous > 0 ? std::vector<int>{previous} : std::vector<int>{})
            : pending_branch_ends;
    }
    return pending_branch_ends.empty()
        ? previous : pending_branch_ends.front();
}

Graph build_childhood_timeline(
    const Graph& technical,
    const std::vector<const GraphNode*>& rose_sequence) {
    Graph graph;
    graph.title = technical.title;
    if (rose_sequence.size() < 30) return graph;

    const GraphNode* technical_root = nullptr;
    for (const GraphNode& node : technical.nodes) {
        if (node.kind == NodeKind::Quest) {
            technical_root = &node;
            break;
        }
    }
    GraphNode root;
    root.id = 1;
    root.kind = NodeKind::Quest;
    root.badge = "Quest start";
    root.title = technical_root
        ? clean_names(technical_root->title)
        : clean_names(technical.title);
    graph.nodes.push_back(std::move(root));

    const GraphNode* intro = source_with_detail(technical, "Play movie Intro.bik");
    const GraphNode* murgo_pitch = source_with_detail(
        technical, "Cutscene ID: QC010_MurgoPitch1");
    const GraphNode* travel = source_with_detail(
        technical, "Travel to Albion / Fairfax Castle Gardens");

    struct Section {
        std::size_t begin;
        std::size_t end;
        const char* title;
    };
    static const Section sections[] = {
        {0, 2, "Opening in Bowerstone Old Town"},
        {2, 9, "Follow Rose to Murgo's crowd"},
        {9, 10, "Earn five gold for the music box"},
        {10, 13, "Return to Murgo and buy the music box"},
        {13, 16, "Go somewhere quiet and make the wish"},
        {16, 18, "Return to the shack and go to bed"},
        {18, 22, "Meet Lucien's guard the next morning"},
        {22, 25, "Travel to Fairfax Castle and follow Jeeves"},
        {25, 27, "Meet Lord Lucien in his study"},
        {27, 30, "Stand in the magic circle"},
    };

    std::unordered_set<std::string> seen_dialogue;
    std::unordered_set<std::string> seen_events;
    std::size_t dialogue_count = 0;
    int previous = graph.nodes.front().id;
    std::vector<int> previous_ends{previous};
    std::set<std::tuple<int, int, std::string>> unique_links;

    auto append_source = [&](std::vector<Beat>& beats,
                             const GraphNode* source) {
        if (!source) return;
        std::vector<Beat> source_beats = story_beats(
            *source, seen_dialogue, seen_events);
        if (!beats.empty() && !source_beats.empty()) {
            Beat boundary;
            boundary.source_boundary = true;
            beats.push_back(std::move(boundary));
        }
        beats.insert(beats.end(),
                     std::make_move_iterator(source_beats.begin()),
                     std::make_move_iterator(source_beats.end()));
    };

    float main_y = estimated_node_height(graph.nodes.front()) + 105.0f;
    auto add_step_node = [&](const std::string& title,
                             const std::string& step_badge,
                             float x, float& y) {
        GraphNode node;
        node.id = int(graph.nodes.size()) + 1;
        node.kind = NodeKind::State;
        node.badge = step_badge;
        node.title = title;
        node.x = x;
        node.y = y;
        y += estimated_node_height(node) + 105.0f;
        graph.nodes.push_back(std::move(node));
        return graph.nodes.back().id;
    };

    for (std::size_t section_index = 0;
         section_index < std::size(sections); ++section_index) {
        const Section& section = sections[section_index];
        std::vector<Beat> beats;
        if (section_index == 1) {
            std::vector<Beat> prefix;
            for (std::size_t i = section.begin; i < 5; ++i) {
                append_source(prefix, rose_sequence[i]);
            }

            std::vector<Beat> conditional = story_beats(
                *rose_sequence[5], seen_dialogue, seen_events);
            std::vector<Beat> creep_scene;
            std::vector<Beat> beckons_scene;
            std::optional<Beat> crowd_movement;
            enum class ConditionalScene { None, Creep, Beckons, Ignore };
            ConditionalScene active_scene = ConditionalScene::None;
            for (const Beat& beat : conditional) {
                if (starts_ci(beat.title, "Cutscene: ")) {
                    if (contains_ci(beat.title, "Rose Creep")) {
                        active_scene = creep_scene.empty()
                            ? ConditionalScene::Creep
                            : ConditionalScene::Ignore;
                        if (active_scene == ConditionalScene::Creep) {
                            creep_scene.push_back(beat);
                        }
                    } else if (contains_ci(
                                   beat.title, "Rose Beckons To Crowd")) {
                        active_scene = ConditionalScene::Beckons;
                        beckons_scene.push_back(beat);
                    } else {
                        active_scene = ConditionalScene::Ignore;
                    }
                    continue;
                }
                if (beat.kind == BeatKind::ActorAction &&
                    contains_ci(beat.title, "Rose In Crowd Marker")) {
                    crowd_movement = beat;
                    active_scene = ConditionalScene::None;
                    continue;
                }
                if (beat.kind != BeatKind::Dialogue) continue;
                if (active_scene == ConditionalScene::Creep) {
                    creep_scene.push_back(beat);
                } else if (active_scene == ConditionalScene::Beckons) {
                    beckons_scene.push_back(beat);
                }
            }

            std::vector<Beat> suffix;
            for (std::size_t i = 6; i < section.end; ++i) {
                append_source(suffix, rose_sequence[i]);
                if (i == 6) append_source(suffix, murgo_pitch);
            }

            const int step_id = add_step_node(
                section.title, "Quest step 2", 0.0f, main_y);
            for (int branch_end : previous_ends) {
                add_link(graph, unique_links, branch_end, step_id, "");
            }
            int prefix_end = append_story_beat_nodes(
                graph, prefix, step_id, 0.0f, main_y,
                dialogue_count, unique_links);

            const int in_crowd_condition = add_step_node(
                "Is Sparrow already in Murgo's crowd?", "Condition",
                0.0f, main_y);
            graph.nodes.back().metadata.push_back(
                "Lua condition: self.ParentQuest.HeroInCrowd");
            add_link(graph, unique_links, prefix_end, in_crowd_condition, "");

            const int through_arch_condition = add_step_node(
                "Has Sparrow passed through the arch?", "Condition",
                0.0f, main_y);
            graph.nodes.back().metadata.push_back(
                "Lua condition: self.ParentQuest.HeroThroughArch");
            add_link(graph, unique_links, in_crowd_condition,
                     through_arch_condition, "No");

            const float branch_start_y = main_y;
            float through_arch_y = branch_start_y;
            std::vector<int> through_arch_ends;
            append_story_beat_nodes(
                graph, creep_scene, through_arch_condition, -430.0f,
                through_arch_y, dialogue_count, unique_links,
                "Yes", &through_arch_ends);

            std::vector<Beat> beckoned_path = beckons_scene;
            if (crowd_movement) {
                Beat boundary;
                boundary.source_boundary = true;
                beckoned_path.push_back(std::move(boundary));
                beckoned_path.push_back(*crowd_movement);
            }
            if (!creep_scene.empty()) {
                Beat boundary;
                boundary.source_boundary = true;
                beckoned_path.push_back(std::move(boundary));
                beckoned_path.insert(beckoned_path.end(),
                                     creep_scene.begin(), creep_scene.end());
            }
            float beckoned_y = branch_start_y;
            std::vector<int> beckoned_ends;
            append_story_beat_nodes(
                graph, beckoned_path, through_arch_condition, 430.0f,
                beckoned_y, dialogue_count, unique_links,
                "No", &beckoned_ends);

            main_y = std::max(through_arch_y, beckoned_y) + 105.0f;
            const std::size_t suffix_begin = graph.nodes.size();
            std::vector<int> suffix_ends;
            append_story_beat_nodes(
                graph, suffix, 0, 0.0f, main_y, dialogue_count,
                unique_links, {}, &suffix_ends);
            if (suffix_begin < graph.nodes.size()) {
                const int suffix_first = graph.nodes[suffix_begin].id;
                add_link(graph, unique_links, in_crowd_condition,
                         suffix_first, "Yes");
                for (int branch_end : through_arch_ends) {
                    add_link(graph, unique_links, branch_end, suffix_first, "");
                }
                for (int branch_end : beckoned_ends) {
                    add_link(graph, unique_links, branch_end, suffix_first, "");
                }
                previous = suffix_ends.empty()
                    ? suffix_first : suffix_ends.front();
                previous_ends = suffix_ends.empty()
                    ? std::vector<int>{suffix_first} : suffix_ends;
            } else {
                previous = in_crowd_condition;
                previous_ends = {in_crowd_condition};
            }
            continue;
        }
        if (section_index == 0) append_source(beats, intro);
        if (section_index == 7) append_source(beats, travel);
        for (std::size_t i = section.begin; i < section.end; ++i) {
            append_source(beats, rose_sequence[i]);



            if (section_index == 1 && i == 6) {
                append_source(beats, murgo_pitch);
            }
        }
        if (beats.empty()) continue;

        if (section_index == 2) {
            std::vector<Beat> job_beats[7];
            int current_job = 6;
            for (Beat& beat : beats) {
                if (starts_ci(beat.title, "Cutscene: ")) {
                    current_job = childhood_job_for_event(
                        beat.title, current_job);
                    mark_childhood_alternative(beat);
                }
                job_beats[current_job].push_back(std::move(beat));
            }

            const int hub_id = add_step_node(
                section.title, "Quest step 3", 0.0f, main_y);
            GraphNode& hub = graph.nodes.back();
            const float hub_empty_height =
                main_y - hub.y - 105.0f;
            hub.details = {
                "Complete all five jobs. They may be done in any order:",
                "Pose for Barnum's picture",
                "Clear the beetles from Balthazar's warehouse",
                "Deliver Monty's love letter",
                "Find Derek's arrest warrants",
                "Retrieve Pete's stolen bottle",
            };
            main_y += estimated_node_height(hub) - hub_empty_height;
            for (int branch_end : previous_ends) {
                add_link(graph, unique_links, branch_end, hub_id, "");
            }

            static const char* job_titles[] = {
                "Pose for Barnum's picture",
                "Clear the beetles from Balthazar's warehouse",
                "Deliver Monty's love letter",
                "Find Derek's arrest warrants",
                "Retrieve Pete's stolen bottle",
                "Help the dog after Rex's attack",
                "Rose reports the current gold total",
            };
            std::vector<int> required_jobs;
            std::vector<int> phase_events;
            float branch_end_y = main_y;
            for (int job = 0; job < 7; ++job) {
                if (job_beats[job].empty()) continue;
                const float lane_x = float(job - 3) * 620.0f;
                float lane_y = main_y;
                const std::string lane_badge = job < 5
                    ? "Step 3 job - any order"
                    : "Step 3 event";
                const int lane_start = add_step_node(
                    job_titles[job], lane_badge, lane_x, lane_y);
                add_link(graph, unique_links, hub_id, lane_start,
                         job < 5 ? "any order"
                                 : job == 5 ? "after the third job"
                                            : "during this step");
                const int lane_end = append_story_beat_nodes(
                    graph, job_beats[job], lane_start, lane_x, lane_y,
                    dialogue_count, unique_links);
                if (job < 5) required_jobs.push_back(lane_end);
                else phase_events.push_back(lane_end);
                branch_end_y = std::max(branch_end_y, lane_y);
            }

            main_y = branch_end_y + 105.0f;
            const int collected_id = add_step_node(
                "Five gold collected", "Step 3 complete", 0.0f, main_y);
            GraphNode& collected = graph.nodes.back();
            const float collected_empty_height =
                main_y - collected.y - 105.0f;
            collected.details.push_back(
                "The story continues after all five jobs are complete.");
            main_y += estimated_node_height(collected) -
                      collected_empty_height;
            for (int job_id : required_jobs) {
                add_link(graph, unique_links, job_id, collected_id,
                         "job complete");
            }
            for (int event_id : phase_events) {
                add_link(graph, unique_links, event_id, collected_id,
                         "during the five-gold phase");
            }
            previous = collected_id;
            previous_ends = {collected_id};
            continue;
        }

        const int step_id = add_step_node(
            section.title,
            "Quest step " + std::to_string(section_index + 1),
            0.0f, main_y);
        for (int branch_end : previous_ends) {
            add_link(graph, unique_links, branch_end, step_id, "");
        }
        std::vector<int> section_ends;
        previous = append_story_beat_nodes(
            graph, beats, step_id, 0.0f, main_y,
            dialogue_count, unique_links, {}, &section_ends);
        previous_ends = section_ends.empty()
            ? std::vector<int>{previous} : std::move(section_ends);
    }

    std::vector<Beat> ending;
    Beat fall;
    fall.kind = BeatKind::Task;
    fall.title =
        "Cinematic: Lucien shoots Sparrow, who falls from the castle";
    fall.subtitle = "Story event";
    fall.metadata.push_back("Movie asset: Fall_m.bik / Fall_f.bik");
    ending.push_back(std::move(fall));
    Beat new_beginnings;
    new_beginnings.kind = BeatKind::Task;
    new_beginnings.title =
        "Cinematic: Theresa rescues Sparrow; time advances to adulthood";
    new_beginnings.subtitle = "Story event";
    new_beginnings.metadata.push_back("Movie asset: New Beginnings.bik");
    ending.push_back(std::move(new_beginnings));
    Beat complete;
    complete.kind = BeatKind::Ending;
    complete.title = "Quest complete";
    ending.push_back(std::move(complete));
    const int ending_step = add_step_node(
        "Childhood ends", "Quest step 11", 0.0f, main_y);
    for (int branch_end : previous_ends) {
        add_link(graph, unique_links, branch_end, ending_step, "");
    }
    append_story_beat_nodes(
        graph, ending, ending_step, 0.0f, main_y,
        dialogue_count, unique_links);

    graph.flow_steps = 11;
    graph.dialogue_lines = dialogue_count;
    for (const GraphNode& node : graph.nodes) {
        for (const std::string& metadata : node.metadata) {
            if (starts_ci(metadata, "Related audio: ")) ++graph.audio_matches;
        }
    }
    return graph;
}

std::string reference_text(const ReferenceCatalog& references,
                           const std::string& tag) {
    auto found = references.localized_text.find(tag);
    if (found != references.localized_text.end()) return trim(found->second);
    const std::string wanted = lower_ascii(tag);
    for (const auto& entry : references.localized_text) {
        if (lower_ascii(entry.first) == wanted) return trim(entry.second);
    }
    return {};
}

void append_dialogue_metadata(GraphNode& node,
                              const ReferenceCatalog& references,
                              const std::string& tag) {
    node.metadata.push_back("Dialogue ID: " + tag);
    const std::string wanted = lower_ascii(tag);
    for (const auto& entry : references.audio_by_dialogue) {
        if (lower_ascii(entry.first) != wanted) continue;
        for (const std::string& audio : entry.second) {
            node.metadata.push_back("Related audio: " + audio);
        }
        break;
    }
}

const std::vector<WorldEntityPlacement>* find_world_placements(
    const ReferenceCatalog& references, const std::string& marker) {
    const std::string wanted = lower_ascii(marker);
    const auto direct = references.world_entities.find(wanted);
    if (direct != references.world_entities.end()) return &direct->second;
    for (const auto& entry : references.world_entities) {
        if (lower_ascii(entry.first) == wanted) return &entry.second;
    }
    return nullptr;
}

std::string format_world_position(const WorldEntityPlacement& placement) {
    std::ostringstream text;
    text << std::fixed << std::setprecision(2)
         << "X " << placement.x << ", Y " << placement.y
         << ", Z " << placement.z;
    return text.str();
}

void append_world_placement_details(std::vector<std::string>& details,
                                    const ReferenceCatalog& references,
                                    const std::string& marker) {
    details.push_back("Dig spot marker: " + marker);
    const std::vector<WorldEntityPlacement>* placements =
        find_world_placements(references, marker);
    if (!placements || placements->empty()) {
        details.push_back("Dig spot coordinates: not found in indexed levels");
        return;
    }
    for (std::size_t index = 0; index < placements->size(); ++index) {
        const WorldEntityPlacement& placement = (*placements)[index];
        const std::string prefix = placements->size() == 1
            ? "Dig spot" : "Dig spot " + std::to_string(index + 1);
        details.push_back(prefix + " coordinates: " +
                          format_world_position(placement));
    }
}

Graph build_frankenbride_timeline(const Graph& technical,
                                  const ReferenceCatalog& references) {
    Graph graph;
    graph.title = technical.title;
    std::set<std::tuple<int, int, std::string>> unique_links;

    auto add_node = [&](NodeKind kind, std::string badge,
                        std::string title, std::string subtitle,
                        std::vector<std::string> details,
                        float x, float& y) {
        GraphNode node;
        node.id = int(graph.nodes.size()) + 1;
        node.kind = kind;
        node.badge = std::move(badge);
        node.title = std::move(title);
        node.subtitle = std::move(subtitle);
        node.details = std::move(details);
        node.x = x;
        node.y = y;
        y += estimated_node_height(node) + 105.0f;
        graph.nodes.push_back(std::move(node));
        return graph.nodes.back().id;
    };

    auto add_dialogue = [&](const std::string& title,
                            const std::string& scene,
                            const std::vector<std::pair<std::string,
                                                       std::string>>& lines,
                            float x, float& y) {
        GraphNode node;
        node.id = int(graph.nodes.size()) + 1;
        node.kind = NodeKind::Thread;
        node.badge = "Dialogue";
        node.title = title;
        node.subtitle = scene.empty() ? std::string() : "Scene: " + scene;
        node.x = x;
        node.y = y;
        for (const auto& line : lines) {
            std::string text = reference_text(references, line.second);
            if (text.empty()) text = "[Missing text: " + line.second + "]";
            node.details.push_back(line.first + ": \"" + text + "\"");
            append_dialogue_metadata(node, references, line.second);
            ++graph.dialogue_lines;
        }
        y += estimated_node_height(node) + 105.0f;
        graph.nodes.push_back(std::move(node));
        return graph.nodes.back().id;
    };

    auto connect = [&](int from, int to, const std::string& label = {}) {
        add_link(graph, unique_links, from, to, label);
    };

    float main_y = 0.0f;
    std::string quest_name = reference_text(
        references, "TEXT_QUEST_QO570_NAME");
    if (quest_name.empty()) quest_name = "Love Hurts";
    const int root = add_node(
        NodeKind::Quest, "Quest start",
        quest_name + " - Resurrect Lady Grey",
        {},
        {"Quest: QO570 Franken Bride",
         "Primary actors: Hero, Victor (the Grave Keeper), Lady Grey",
         "The final choice is made by leaving or remaining in the laboratory."},
        0.0f, main_y);
    int previous = root;

    auto add_step = [&](const std::string& badge, const std::string& title,
                        std::vector<std::string> details) {
        const int id = add_node(NodeKind::State, badge, title, {},
                                std::move(details), 0.0f, main_y);
        connect(previous, id);
        previous = id;
        return id;
    };
    auto add_main_dialogue = [&](const std::string& title,
                                 const std::string& scene,
                                 const std::vector<std::pair<std::string,
                                                            std::string>>& lines) {
        const int id = add_dialogue(title, scene, lines, 0.0f, main_y);
        connect(previous, id);
        previous = id;
        return id;
    };
    auto add_main_action = [&](const std::string& badge,
                               const std::string& title,
                               std::vector<std::string> details) {
        const int id = add_node(NodeKind::Action, badge, title, {},
                                std::move(details), 0.0f, main_y);
        connect(previous, id);
        previous = id;
        return id;
    };

    add_step("Quest step 1", "Meet the Grave Keeper",
             {"Location: Victor's mansion, Bowerstone Cemetery",
              "Interact with the front door and speak through the peephole."});
    add_main_dialogue(
        "Victor answers from behind the door", "GK Intro Pt 1",
        {{"Victor", "TEXT_QUEST_QO570_V2_INTRO_10"},
         {"Victor", "TEXT_QUEST_QO570_V2_INTRO_20"}});
    add_main_dialogue(
        "Victor offers the quest", "GK Intro Pt 2",
        {{"Victor", "TEXT_QUEST_QO570_V2_INTRO_30"},
         {"Victor", "TEXT_QUEST_QO570_V2_INTRO_40"},
         {"Victor", "TEXT_QUEST_QO570_V2_INTRO_50"},
         {"Victor", "TEXT_QUEST_QO570_V2_INTRO_60"}});
    add_main_dialogue(
        "Accept Victor's scientific expedition", "GK Accept Quest",
        {{"Victor", "TEXT_QUEST_QO570_V2_ACCEPT_QUEST_10"},
         {"Victor", "TEXT_QUEST_QO570_V2_ACCEPT_QUEST_20"},
         {"Victor", "TEXT_QUEST_QO570_V2_ACCEPT_QUEST_30"}});

    std::vector<std::string> lower_body_details = {
        "Location: Hobbe Cave in Rookridge",
        "Level asset: Caves\\Dunecrest\\HobbeCave",
    };
    append_world_placement_details(lower_body_details, references,
                                   "QO570_DigSpot");
    lower_body_details.push_back(
        "Dig there and collect ZombieBrideLegs.");
    lower_body_details.push_back(
        "Return to Victor in Bowerstone Cemetery.");
    add_step("Quest step 2", "Recover the lower body",
             std::move(lower_body_details));
    add_main_action(
        "Objective", "Dig up the first body part and return it to Victor",
        {"Victor leaves the mansion door open after the lower body is found.",
         "The Hero hands over ZombieBrideLegs."});
    add_main_dialogue(
        "Victor reveals that the body is Lady Grey", "Give GK Lower Body",
        {{"Victor", "TEXT_QUEST_QO570_V2_BODY_ONE_10"},
         {"Victor", "TEXT_QUEST_QO570_V2_BODY_ONE_20"},
         {"Victor", "TEXT_QUEST_QO570_V2_BODY_ONE_30"},
         {"Victor", "TEXT_QUEST_QO570_V2_BODY_ONE_40"},
         {"Victor", "TEXT_QUEST_QO570_V2_BODY_ONE_50"},
         {"Victor", "TEXT_QUEST_QO570_V2_BODY_ONE_60"}});

    const int gender_branch_from = previous;
    float female_y = main_y;
    const int female_line = add_dialogue(
        "If female - Victor comments on the Hero", "GK Extra Line If Female",
        {{"Victor", "TEXT_QUEST_QO570_V2_WAIT_THREE_FEMALE"}},
        380.0f, female_y);
    connect(gender_branch_from, female_line, "Female Hero");
    main_y = female_y + 45.0f;
    previous = female_line;

    const int upper_body_step = add_step(
        "Quest step 3", "Recover the upper body",
        {"Location: Twinblade's Tomb, between Bloodstone and Wraithmarsh",
         "Level asset: Tombs\\Wraithmarsh\\WraithmarshToBloodstoneTomb",
         "Open QO570_Coffin_V_3 and collect ZombieBrideTorso and QO570_Note2.",
         "Return to Victor in Bowerstone Cemetery."});
    connect(gender_branch_from, upper_body_step, "Male Hero");
    add_main_action(
        "Objective", "Open the coffin and bring Lady Grey's torso to Victor",
        {"A Hollow Men creature generator is triggered inside the tomb.",
         "The Hero hands over ZombieBrideTorso."});
    add_main_dialogue(
        "Victor receives Lady Grey's upper body", "Give GK Upper Body",
        {{"Victor", "TEXT_QUEST_QO570_V2_BODY_TWO_10"},
         {"Victor", "TEXT_QUEST_QO570_V2_BODY_TWO_20"},
         {"Victor", "TEXT_QUEST_QO570_V2_BODY_TWO_30"},
         {"Victor", "TEXT_QUEST_QO570_V2_BODY_TWO_40"},
         {"Victor", "TEXT_QUEST_QO570_V2_BODY_TWO_50"}});

    add_step("Quest step 4", "Recover Lady Grey's head",
             {"Location: Lady Grey's Tomb, reached through Fairfax Gardens",
              "Level asset: Tombs\\BWSCemetery\\LadyGreysTomb",
              "Follow QO570_LastSarcMarker and open the marked sarcophagus.",
              "Return to Victor's laboratory in Bowerstone Cemetery."});
    add_main_action(
        "Objective", "Open the sarcophagus and return Lady Grey's head",
        {"The tomb portcullis unlocks after the sarcophagus is opened.",
         "Victor waits in the basement laboratory with the body prepared."});
    add_main_dialogue(
        "Victor receives the final body part", "Give GK The Head",
        {{"Victor", "TEXT_QUEST_QO570_V2_BODY_THREE_10"}});

    add_step("Quest step 5", "Resurrect Lady Grey",
             {"Location: Victor's basement laboratory, Bowerstone Cemetery",
              "The laboratory door locks and fast travel is blocked during the scene."});
    add_main_dialogue(
        "Victor explains the resurrection and love spell",
        "The Resurrection Scene",
        {{"Victor", "TEXT_QUEST_QO570_V2_LAB_10"},
         {"Victor", "TEXT_QUEST_QO570_V2_LAB_20"},
         {"Victor", "TEXT_QUEST_QO570_V2_LAB_30"},
         {"Victor", "TEXT_QUEST_QO570_V2_LAB_40"},
         {"Victor", "TEXT_QUEST_QO570_V2_LAB_50"},
         {"Victor", "TEXT_QUEST_QO570_V2_LAB_70"},
         {"Victor", "TEXT_QUEST_QO570_V2_LAB_80"}});
    add_main_action(
        "Story event", "Victor activates the Table of Life; Lady Grey rises",
        {"Victor moves around the table and plays the 'She Is Alive' animation.",
         "Lady Grey plays 'Table Of Life Out Of', rises, and moves into position.",
         "Music: MUSIC_QO570_RESURRECTION_FRANKENWIFE_01"});
    add_main_dialogue(
        "The love spell makes Lady Grey fall for the Hero",
        "GK End Scene Pt 4",
        {{"Victor", "TEXT_QUEST_QO570_V2_RESURRECTED_10"},
         {"Victor", "TEXT_QUEST_QO570_V2_RESURRECTED_20"},
         {"Lady Grey", "TEXT_QUEST_QO570_V2_RESURRECTED_30"},
         {"Victor", "TEXT_QUEST_QO570_V2_RESURRECTED_40"},
         {"Lady Grey", "TEXT_QUEST_QO570_V2_RESURRECTED_50"},
         {"Victor", "TEXT_QUEST_QO570_V2_RESURRECTED_60"}});

    add_step("Quest step 6", "Decide who Lady Grey will love",
             {"A 45-second timer begins when the laboratory door unlocks.",
              "The decision is made through movement; there is no menu prompt."});
    add_main_action(
        "Timed choice", "45 seconds: leave the laboratory or remain with Lady Grey",
        {"Leave through either outside-laboratory trigger before time expires: Lady Grey marries Victor.",
         "Remain until the timer expires: Lady Grey stays in love with the Hero."});
    add_main_dialogue(
        "Lady Grey flirts while Victor begs the Hero to leave",
        "GK End Scene Pt 5",
        {{"Lady Grey", "TEXT_QUEST_QO570_V2_TIMER_10"},
         {"Victor", "TEXT_QUEST_QO570_V2_TIMER_20"},
         {"Lady Grey", "TEXT_QUEST_QO570_V2_TIMER_30"}});

    const int timer_branch_from = previous;
    const std::string variant_text = reference_text(
        references, "TEXT_QUEST_QO570_V2_TIMER_40");
    Beat variant_beat;
    variant_beat.title = "Victor: " + variant_text;
    const std::optional<GenderDialogue> variants = gender_dialogue(variant_beat);
    const std::string male_text = variants
        ? variants->male : variant_text;
    const std::string female_text = variants
        ? variants->female : variant_text;
    const float timer_gender_y = main_y;
    float timer_male_y = timer_gender_y;
    float timer_female_y = timer_gender_y;
    const int timer_male = add_node(
        NodeKind::Thread, "If male", "Victor pleads with a male Hero",
        "Scene: GK End Scene Pt 5", {"Victor: \"" + male_text + "\""},
        -380.0f, timer_male_y);
    append_dialogue_metadata(graph.nodes.back(), references,
                             "TEXT_QUEST_QO570_V2_TIMER_40_HM");
    ++graph.dialogue_lines;
    const int timer_female = add_node(
        NodeKind::Thread, "If female", "Victor pleads with a female Hero",
        "Scene: GK End Scene Pt 5", {"Victor: \"" + female_text + "\""},
        380.0f, timer_female_y);
    append_dialogue_metadata(graph.nodes.back(), references,
                             "TEXT_QUEST_QO570_V2_TIMER_40_HF");
    ++graph.dialogue_lines;
    connect(timer_branch_from, timer_male, "Male Hero");
    connect(timer_branch_from, timer_female, "Female Hero");
    main_y = std::max(timer_male_y, timer_female_y) + 45.0f;
    previous = timer_male;
    const int timer_continues = add_main_dialogue(
        "The timer approaches zero", "GK End Scene Pt 5",
        {{"Lady Grey", "TEXT_QUEST_QO570_V2_TIMER_50"},
         {"Victor", "TEXT_QUEST_QO570_V2_TIMER_60"}});
    connect(timer_female, timer_continues);

    const int ending_choice = add_node(
        NodeKind::State, "Ending choice",
        "Does the Hero leave before 45 seconds expire?", {},
        {"YES: leave the laboratory; Lady Grey and Victor marry.",
         "NO: remain for 45 seconds; Lady Grey stays in love with the Hero."},
        0.0f, main_y);
    connect(previous, ending_choice);

    constexpr float kEndingLane = 650.0f;
    float leave_y = main_y;
    const int leave_action = add_node(
        NodeKind::Action, "Leave within 45 seconds",
        "Lady Grey notices Victor after the Hero exits", {},
        {"The laboratory door closes and Lady Grey turns toward Victor.",
         "The love spell transfers to the next person she sees."},
        -kEndingLane, leave_y);
    connect(ending_choice, leave_action, "Yes - leave");
    const int leave_dialogue = add_dialogue(
        "Lady Grey and Victor fall in love", "GK End Scene Pt 7",
        {{"Lady Grey", "TEXT_QUEST_QO570_V2_GOOD_10"},
         {"Victor", "TEXT_QUEST_QO570_V2_GOOD_20"}},
        -kEndingLane, leave_y);
    connect(leave_action, leave_dialogue);
    const int leave_ending = add_node(
        NodeKind::Quest, "Quest complete: Good ending",
        "Lady Grey marries Victor", {},
        {"Victor and Lady Grey marry.",
         "Morality: +10 good",
         "Lady Grey's quest layer is removed after the quest.",
         "Good epilogue; quest complete."},
        -kEndingLane, leave_y);
    connect(leave_dialogue, leave_ending);

    float stay_y = main_y;
    const int stay_action = add_node(
        NodeKind::Action, "Stay for 45 seconds",
        "The timer expires; Victor loses Lady Grey", {},
        {"Lady Grey remains focused on the Hero.",
         "Victor opens the door, sprints out of the laboratory, and later disappears."},
        kEndingLane, stay_y);
    connect(ending_choice, stay_action, "No - stay");
    const int stay_dialogue = add_dialogue(
        "Lady Grey chooses the Hero; Victor despairs", "GK End Scene Pt 6",
        {{"Lady Grey", "TEXT_QUEST_QO570_V2_EVIL_10"},
         {"Victor", "TEXT_QUEST_QO570_V2_EVIL_20"},
         {"Victor", "TEXT_QUEST_QO570_V2_EVIL_30"}},
        kEndingLane, stay_y);
    connect(stay_action, stay_dialogue);
    const int stay_ending = add_node(
        NodeKind::Quest, "Quest complete: Evil ending",
        "Lady Grey remains available to the Hero", {},
        {"Morality: -10 evil",
         "Lady Grey remains alive in the world with normal social behaviours.",
         "The Hero may court and marry her later through the normal relationship system.",
         "Evil epilogue; quest complete."},
        kEndingLane, stay_y);
    connect(stay_dialogue, stay_ending);

    graph.flow_steps = 6;
    for (const GraphNode& node : graph.nodes) {
        for (const std::string& metadata : node.metadata) {
            if (starts_ci(metadata, "Related audio: ")) ++graph.audio_matches;
        }
    }
    return graph;
}

}

Graph BuildStoryGraph(const std::string& title,
                      const std::string& decompiled_lua,
                      const ReferenceCatalog& references) {
    const Graph technical = BuildGraph(title, decompiled_lua, references);
    const bool frankenbride = contains_ci(title, "frankenbride") &&
        decompiled_lua.find("QO570_FrankenBride") != std::string::npos;
    if (frankenbride) {
        Graph graph = build_frankenbride_timeline(technical, references);
        attach_completion_rewards(graph, technical, references);
        return graph;
    }
    const bool childhood = contains_ci(title, "childhood") &&
        decompiled_lua.find("QC010_Childhood") != std::string::npos;
    if (childhood) {
        const std::vector<const GraphNode*> primary =
            primary_entity_sequence(technical);
        if (primary.size() >= 30) {
            Graph graph = build_childhood_timeline(technical, primary);
            attach_completion_rewards(graph, technical, references);
            return graph;
        }
    }
    Graph graph;
    graph.title = technical.title;
    if (technical.nodes.empty()) return graph;

    std::unordered_map<int, std::vector<int>> story_by_technical;
    std::unordered_map<int, std::vector<int>> story_exits_by_technical;
    std::unordered_set<std::string> seen_dialogue;
    std::unordered_set<std::string> seen_events;
    std::size_t dialogue_count = 0;
    int root_story = 0;
    std::set<std::tuple<int, int, std::string>> unique_links;

    for (const GraphNode& source : technical.nodes) {
        if (source.kind == NodeKind::Quest) {
            GraphNode node;
            node.id = int(graph.nodes.size()) + 1;
            node.kind = NodeKind::Quest;
            node.badge = "Quest start";
            node.title = clean_names(source.title);
            node.x = source.x;
            node.y = source.y;
            graph.nodes.push_back(std::move(node));
            root_story = graph.nodes.back().id;
            story_by_technical[source.id].push_back(root_story);
            story_exits_by_technical[source.id] = {root_story};
            const std::vector<Beat> setup_beats = story_beats(
                source, seen_dialogue, seen_events);
            if (!setup_beats.empty()) {
                const std::size_t first_setup = graph.nodes.size();
                float setup_y = source.y + estimated_node_height(
                    graph.nodes.back()) + 105.0f;
                std::vector<int> setup_exits;
                append_story_beat_nodes(
                    graph, setup_beats, root_story, source.x, setup_y,
                    dialogue_count, unique_links, "setup", &setup_exits);
                if (!setup_exits.empty()) {
                    story_exits_by_technical[source.id] =
                        std::move(setup_exits);
                }
                for (std::size_t i = first_setup; i < graph.nodes.size(); ++i) {
                    story_by_technical[source.id].push_back(graph.nodes[i].id);
                }
            }
            continue;
        }

        const std::vector<Beat> beats = story_beats(
            source, seen_dialogue, seen_events);
        if (beats.empty()) continue;
        const std::size_t first_new_node = graph.nodes.size();
        float source_y = source.y;
        std::vector<int> source_exits;
        append_story_beat_nodes(
            graph, beats, 0, source.x, source_y, dialogue_count,
            unique_links, {}, &source_exits);
        for (std::size_t i = first_new_node; i < graph.nodes.size(); ++i) {
            story_by_technical[source.id].push_back(graph.nodes[i].id);
        }
        story_exits_by_technical[source.id] = std::move(source_exits);
    }

    std::unordered_map<int, std::vector<int>> outgoing;
    std::unordered_map<int, std::vector<int>> incoming;
    for (const GraphLink& link : technical.links) {
        outgoing[link.from_node].push_back(link.to_node);
        incoming[link.to_node].push_back(link.from_node);
    }

    for (const auto& entry : story_by_technical) {
        if (entry.second.empty()) continue;
        const int source_technical = entry.first;
        const auto exits = story_exits_by_technical.find(source_technical);
        const std::vector<int>& source_story =
            exits != story_exits_by_technical.end() && !exits->second.empty()
                ? exits->second : entry.second;
        std::deque<int> queue;
        std::unordered_set<int> visited;
        for (int next : outgoing[source_technical]) queue.push_back(next);
        while (!queue.empty()) {
            const int current = queue.front();
            queue.pop_front();
            if (!visited.insert(current).second) continue;
            const auto found = story_by_technical.find(current);
            if (found != story_by_technical.end() && !found->second.empty()) {
                for (int source_exit : source_story) {
                    add_link(graph, unique_links, source_exit,
                             found->second.front(),
                             is_decision(graph, source_exit) ? "Yes" : "");
                }
                continue;
            }
            for (int next : outgoing[current]) queue.push_back(next);
        }
    }

    for (const auto& entry : story_by_technical) {
        const std::vector<int>& beats = entry.second;
        for (std::size_t i = 0; i < beats.size(); ++i) {
            const int decision = beats[i];
            if (!is_decision(graph, decision)) continue;
            int previous = 0;
            if (i > 0) {
                previous = beats[i - 1];
            } else {
                std::deque<int> queue;
                std::unordered_set<int> visited;
                for (int prior : incoming[entry.first]) queue.push_back(prior);
                while (!queue.empty() && previous == 0) {
                    const int current = queue.front();
                    queue.pop_front();
                    if (!visited.insert(current).second) continue;
                    const auto found = story_by_technical.find(current);
                    if (found != story_by_technical.end() &&
                        !found->second.empty()) {
                        const auto exits =
                            story_exits_by_technical.find(current);
                        previous = exits != story_exits_by_technical.end() &&
                                   !exits->second.empty()
                            ? exits->second.front() : found->second.back();
                        break;
                    }
                    for (int prior : incoming[current]) queue.push_back(prior);
                }
            }
            if (previous > 0 && previous != root_story) {
                add_link(graph, unique_links, decision, previous, "No");
            }
        }
    }

    std::unordered_map<int, std::vector<int>> forward_story;
    for (const GraphLink& link : graph.links) {
        if (link.label != "No") {
            forward_story[link.from_node].push_back(link.to_node);
        }
    }
    auto condition_reaches_story = [&](int start) {
        std::deque<int> queue;
        std::unordered_set<int> visited;
        for (int next : forward_story[start]) queue.push_back(next);
        while (!queue.empty()) {
            const int current = queue.front();
            queue.pop_front();
            if (!visited.insert(current).second) continue;
            if (!is_decision(graph, current)) return true;
            for (int next : forward_story[current]) queue.push_back(next);
        }
        return false;
    };
    std::unordered_set<int> remove_conditions;
    for (const GraphNode& node : graph.nodes) {
        if (node.badge == "Condition" &&
            !condition_reaches_story(node.id)) {
            remove_conditions.insert(node.id);
        }
    }
    if (!remove_conditions.empty()) {
        graph.links.erase(
            std::remove_if(graph.links.begin(), graph.links.end(),
                           [&](const GraphLink& link) {
                               return remove_conditions.count(link.from_node) ||
                                      remove_conditions.count(link.to_node);
                           }),
            graph.links.end());
        graph.nodes.erase(
            std::remove_if(graph.nodes.begin(), graph.nodes.end(),
                           [&](const GraphNode& node) {
                               return remove_conditions.count(node.id) != 0;
                           }),
            graph.nodes.end());
        for (std::size_t i = 0; i < graph.links.size(); ++i) {
            graph.links[i].id = int(i) + 1;
        }
    }

    if (root_story > 0) {
        GraphNode* root = nullptr;
        for (GraphNode& node : graph.nodes) {
            if (node.id == root_story) {
                root = &node;
                break;
            }
        }
        if (root) {
            root->x = 0.0f;
            root->y = 0.0f;
        }
    }

    std::vector<int> layout_order;
    layout_branching_story(graph, root_story, layout_order);

    std::size_t step_number = 0;
    for (int id : layout_order) {
        for (GraphNode& node : graph.nodes) {
            if (node.id != id) continue;
            const std::string semantic = node.badge;
            if (node.subtitle.empty()) {
                if (semantic == "Condition") node.subtitle = "Decision (Yes / No)";
                else if (semantic == "Objective") node.subtitle = "Objective";
                else if (semantic == "Reward") node.subtitle = "Reward";
                else if (semantic == "Quest end") node.subtitle = "Quest ending";
                else if (semantic == "Dialogue") node.subtitle = "Dialogue scene";
            }
            if (semantic == "Quest step") {
                node.badge = "Quest step " +
                             std::to_string(++step_number);
            }
            break;
        }
    }

    graph.flow_steps = step_number;
    graph.dialogue_lines = dialogue_count;
    for (const GraphNode& node : graph.nodes) {
        for (const std::string& metadata : node.metadata) {
            if (starts_ci(metadata, "Related audio: ")) ++graph.audio_matches;
        }
    }
    attach_completion_rewards(graph, technical, references);
    return graph;
}

}
