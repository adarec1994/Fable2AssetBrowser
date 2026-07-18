int spawn_level_container_at(
    ID3D11Device* device,
    const std::string& model_path,
    const float engine_pos[3],
    const LevelEdit::ContainerTemplateInfo& info)
{
    if (!info.entity_template || !info.transform_component_field) {
        OutputLog::error(
            "level edit: container template has no injectable entity data");
        return -1;
    }
    if (model_path.empty()) {
        const int add_idx =
            LevelEdit::AddContainerPlacement(model_path, engine_pos, info);
        if (add_idx >= 0) {
            OutputLog::success(
                std::string("level edit: real ") +
                (info.is_dig_spot ? "dig spot" : "container") +
                " queued for GDB injection");
        }
        return add_idx;
    }
    if (!spawn_level_model_at(device, model_path, engine_pos)) return -1;
    std::vector<LevelEdit::Addition> additions;
    LevelEdit::GetAdditions(additions);
    if (additions.empty()) return -1;
    const int add_idx = int(additions.size()) - 1;
    LevelEdit::MarkAdditionAsContainer(add_idx, info);
    OutputLog::success(
        std::string("level edit: real ") +
        (info.is_dig_spot ? "dig spot" : "container") +
        " template queued for GDB injection; click it to edit loot");
    return add_idx;
}
