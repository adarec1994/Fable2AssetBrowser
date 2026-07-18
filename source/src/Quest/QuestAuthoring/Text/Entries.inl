std::vector<std::pair<std::string, std::string>>
AuthoredTextEntries(const AuthoredQuest& quest) {
    std::vector<std::pair<std::string, std::string>> entries;
    entries.emplace_back("Quest_" + quest.quest_id, quest.quest_title);
    if (is_childhood_skip_graph(quest)) {
        entries.emplace_back(patch_accept_tag(quest), "Skip");
    }
    for (const AuthoredNode& node : quest.nodes) {
        switch (node.kind) {
            case AuthoredNodeKind::Dialogue:
            case AuthoredNodeKind::AcceptQuest:
            case AuthoredNodeKind::HoldInteraction:
            case AuthoredNodeKind::ObtainItem:
            case AuthoredNodeKind::ReturnToNpc:
                if (!node.text.empty()) {
                    entries.emplace_back(node_tag(quest, node), node.text);
                }
                break;
            default:
                break;
        }
    }
    return entries;
}
