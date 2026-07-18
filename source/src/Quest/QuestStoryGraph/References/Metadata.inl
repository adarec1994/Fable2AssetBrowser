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
