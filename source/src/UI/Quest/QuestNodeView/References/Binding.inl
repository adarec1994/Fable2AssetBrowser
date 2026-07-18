bool BindActiveLevelReference(const LevelReferenceCandidate& candidate,
                              std::string& error) {
    error.clear();
    if (BlueprintUI::PendingPickPin() != 0) {
        return BlueprintUI::BindPendingPin(
            candidate.level_path, candidate.level_id, candidate.entity_name,
            candidate.entity_hash, candidate.x, candidate.y, candidate.z,
            candidate.model_hashes, candidate.authored_instance, error);
    }
    Quest::AuthoredQuest* quest = active_authored_quest();
    Quest::AuthoredNode* node = quest
        ? Quest::FindAuthoredNode(*quest, g_level_reference_node) : nullptr;
    if (!node) {
        error = "Select a quest node and choose its level reference first.";
        return false;
    }
    Quest::WorldReference reference;
    reference.level_path = candidate.level_path;
    reference.level_id = candidate.level_id;
    reference.entity_name = candidate.entity_name;
    reference.entity_hash = candidate.entity_hash;
    reference.x = candidate.x;
    reference.y = candidate.y;
    reference.z = candidate.z;
    reference.model_hashes = candidate.model_hashes;
    reference.authored_instance = candidate.authored_instance;
    if (!reference.valid()) {
        error = "The selected level entity has no usable GDB name.";
        return false;
    }
    if (g_level_reference_target == LevelReferenceTarget::QuestGiver) {
        if (!candidate.is_npc) {
            error = "Right-click an NPC marker for this node.";
            return false;
        }
        node->entity = std::move(reference);
    } else {
        if (!candidate.is_container) {
            error = "Right-click a container for this item source.";
            return false;
        }
        if (!node->item.valid()) {
            error = "Choose the item in this node first.";
            return false;
        }
        node->item.source = std::move(reference);
    }
    refresh_authored_lua();
    return true;
}

bool GetPendingNpcCreation(NpcCreationRequest& request) {
    if (g_pending_npc_creation.creature_entity == 0 ||
        g_pending_npc_node == 0 || g_pending_npc_quest_id.empty()) {
        request = NpcCreationRequest{};
        return false;
    }
    request = g_pending_npc_creation;
    return true;
}

bool BindCreatedNpcInstance(const LevelReferenceCandidate& candidate,
                            std::string& error) {
    error.clear();
    if (!candidate.is_npc || !candidate.authored_instance) {
        error = "The created quest NPC reference is invalid.";
        return false;
    }
    Quest::AuthoredQuest* quest = nullptr;
    for (Quest::AuthoredQuest& authored : g_authored_quests) {
        if (authored.quest_id == g_pending_npc_quest_id) {
            quest = &authored;
            break;
        }
    }
    Quest::AuthoredNode* node = quest
        ? Quest::FindAuthoredNode(*quest, g_pending_npc_node) : nullptr;
    if (!quest || !node) {
        error = "The quest NPC's source node is no longer available.";
        return false;
    }
    Quest::WorldReference reference;
    reference.level_path = candidate.level_path;
    reference.level_id = candidate.level_id;
    reference.entity_name = candidate.entity_name;
    reference.entity_hash = candidate.entity_hash;
    reference.x = candidate.x;
    reference.y = candidate.y;
    reference.z = candidate.z;
    reference.model_hashes = candidate.model_hashes;
    reference.authored_instance = true;
    if (!reference.valid()) {
        error = "The created NPC has no usable level reference.";
        return false;
    }
    node->entity = std::move(reference);
    CancelPendingNpcCreation();
    refresh_authored_lua();
    return true;
}

void CancelPendingNpcCreation() {
    g_pending_npc_creation = NpcCreationRequest{};
    g_pending_npc_quest_id.clear();
    g_pending_npc_node = 0;
}
