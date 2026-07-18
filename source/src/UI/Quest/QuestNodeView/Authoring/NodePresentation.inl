ed::LinkId authored_link_id(int id) {
    return ed::LinkId(static_cast<std::uintptr_t>(600000 + id));
}

const char* authored_node_badge(Quest::AuthoredNodeKind kind) {
    if (Quest::IsPrerequisiteNode(kind)) return "Prerequisite";
    switch (kind) {
        case Quest::AuthoredNodeKind::QuestStart: return "Quest start";
        case Quest::AuthoredNodeKind::ApproachNpc: return "Trigger";
        case Quest::AuthoredNodeKind::Dialogue: return "Dialogue";
        case Quest::AuthoredNodeKind::AcceptQuest: return "Choice";
        case Quest::AuthoredNodeKind::HoldInteraction:
            return "Interaction";
        case Quest::AuthoredNodeKind::ObtainItem: return "Objective";
        case Quest::AuthoredNodeKind::ReturnToNpc: return "Objective";
        case Quest::AuthoredNodeKind::CompleteQuest: return "Completion";
        case Quest::AuthoredNodeKind::SkipChildhoodEnding:
            return "Existing quest action";
        default: return "Quest";
    }
}

bool authored_node_is_terminal(Quest::AuthoredNodeKind kind) {
    return kind == Quest::AuthoredNodeKind::CompleteQuest ||
           kind == Quest::AuthoredNodeKind::SkipChildhoodEnding;
}

ImVec4 authored_node_colour(Quest::AuthoredNodeKind kind) {
    if (Quest::IsPrerequisiteNode(kind)) {
        return node_color(Quest::NodeKind::State);
    }
    switch (kind) {
        case Quest::AuthoredNodeKind::Dialogue:
            return node_color(Quest::NodeKind::Thread);
        case Quest::AuthoredNodeKind::AcceptQuest:
            return node_color(Quest::NodeKind::Quest);
        case Quest::AuthoredNodeKind::HoldInteraction:
            return node_color(Quest::NodeKind::Quest);
        case Quest::AuthoredNodeKind::ObtainItem:
            return node_color(Quest::NodeKind::State);
        case Quest::AuthoredNodeKind::SkipChildhoodEnding:
            return node_color(Quest::NodeKind::Action);
        default:
            return node_color(Quest::NodeKind::Action);
    }
}

std::string authored_node_summary(const Quest::AuthoredNode& node) {
    switch (node.kind) {
        case Quest::AuthoredNodeKind::QuestStart:
            return {};
        case Quest::AuthoredNodeKind::ApproachNpc:
            return node.entity.valid()
                ? node.entity.entity_name + " within " +
                      std::to_string(node.approach_radius) + " units"
                : "Entity not assigned";
        case Quest::AuthoredNodeKind::Dialogue:
            if (node.text.empty()) return "Dialogue text not entered";
            return node.entity.valid()
                ? node.entity.entity_name + ": \"" + node.text + "\""
                : "Speaker not assigned: \"" + node.text + "\"";
        case Quest::AuthoredNodeKind::AcceptQuest:
            return node.text.empty() ? "Question not entered" : node.text;
        case Quest::AuthoredNodeKind::HoldInteraction:
            return node.text.empty() ? "Prompt text not entered" : node.text;
        case Quest::AuthoredNodeKind::ObtainItem:
            return node.item.valid()
                ? "Get " + std::to_string(node.item_count) + " x " +
                      node.item.display_name
                : "Item not selected";
        case Quest::AuthoredNodeKind::ReturnToNpc:
            return node.entity.valid() ? "Return to " + node.entity.entity_name
                                       : "NPC not assigned";
        case Quest::AuthoredNodeKind::CompleteQuest:
            return "Finish the quest and grant its rewards";
        case Quest::AuthoredNodeKind::SkipChildhoodEnding:
            return "Run QC010's ending movie, cleanup, and adulthood handoff";
        case Quest::AuthoredNodeKind::PrerequisiteStoryProgress:
            return node.story_start.empty() ? "Progress range not set"
                                            : node.story_start + " to " +
                                                  node.story_end;
        case Quest::AuthoredNodeKind::PrerequisiteQuestState:
            return node.other_quest.empty()
                ? "Quest not selected"
                : node.other_quest + " is " +
                      Quest::RequiredQuestStateName(node.quest_state);
        case Quest::AuthoredNodeKind::PrerequisiteGameflowFlag:
            return node.gameflow_flag.empty() ? "Flag not entered"
                                              : node.gameflow_flag;
        case Quest::AuthoredNodeKind::PrerequisiteLuaCondition:
            return node.lua_condition.empty() ? "Condition not entered"
                                              : node.lua_condition;
        case Quest::AuthoredNodeKind::PrerequisiteHeroRequirement:
            return Quest::HeroRequirementKindName(node.hero_requirement);
    }
    return {};
}

bool decode_authored_pin(ed::PinId pin, int& node, bool& input) {
    const std::uintptr_t raw = pin.Get();
    if (raw < 100000) return false;
    const std::uintptr_t value = raw - 100000;
    node = static_cast<int>(value / 2);
    input = (value % 2) == 0;
    return node > 0;
}
