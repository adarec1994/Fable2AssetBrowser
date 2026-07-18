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
