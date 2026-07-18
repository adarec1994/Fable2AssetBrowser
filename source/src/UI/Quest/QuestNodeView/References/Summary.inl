void draw_reference_summary(const Quest::WorldReference& reference) {
    if (!reference.valid()) {
        ImGui::TextDisabled("Not assigned");
        return;
    }
    ImGui::TextWrapped("%s", reference.entity_name.c_str());
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(
                                              ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", reference.level_id.c_str());
    ImGui::TextDisabled("XYZ  %.2f, %.2f, %.2f", reference.x,
                        reference.y, reference.z);
    ImGui::PopStyleColor();
}

std::vector<std::string> model_paths_for_hashes(
    const std::vector<uint32_t>& hashes) {
    std::vector<std::string> paths;
    for (uint32_t hash : hashes) {
        for (const FlatAssetEntry& model : S.all_mdl_files) {
            if (Anim::gdb_model_path_hash(model.full_path) != hash) continue;
            paths.push_back(model.full_path);
            break;
        }
    }
    return paths;
}

void draw_npc_gdb_summary(const Quest::WorldReference& reference) {
    if (reference.model_hashes.empty()) return;
    const std::vector<std::string> models =
        model_paths_for_hashes(reference.model_hashes);
    ImGui::TextUnformatted("GDB model / animation set");
    if (models.empty()) {
        ImGui::TextDisabled("%zu model hash(es)",
                            reference.model_hashes.size());
    } else {
        for (const std::string& model : models) {
            ImGui::TextWrapped("- %s", model.c_str());
        }
    }
    std::vector<std::string> animations;
    for (const Anim::ModelAnimationBinding& binding :
         Anim::model_animation_bindings()) {
        if (std::find(reference.model_hashes.begin(),
                      reference.model_hashes.end(),
                      binding.model_path_hash) ==
            reference.model_hashes.end()) continue;
        const std::string& name = binding.animation_name.empty()
            ? binding.source_name : binding.animation_name;
        if (!name.empty() &&
            std::find(animations.begin(), animations.end(), name) ==
                animations.end()) {
            animations.push_back(name);
        }
    }
    ImGui::TextDisabled("%zu authored animation binding(s)",
                        animations.size());
    const std::size_t shown = std::min<std::size_t>(animations.size(), 6);
    for (std::size_t i = 0; i < shown; ++i) {
        ImGui::TextWrapped("- %s", animations[i].c_str());
    }
    if (animations.size() > shown) {
        ImGui::TextDisabled("...and %zu more", animations.size() - shown);
    }
    ImGui::TextDisabled("Inherited from the selected NPC's GDB entity.");
}
