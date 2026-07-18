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
