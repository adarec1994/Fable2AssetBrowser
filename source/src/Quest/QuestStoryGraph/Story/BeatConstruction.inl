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
