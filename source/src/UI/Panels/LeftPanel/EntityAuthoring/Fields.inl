std::vector<std::pair<uint32_t, std::string>> npc_reference_options(
    uint32_t field_hash) {
    std::unordered_map<uint32_t, std::string> unique;
    for (const auto& entry : g_global_entity_gameplay) {
        auto add = [&](const std::vector<Gdb::EntityGameplayField>& fields) {
            for (const auto& field : fields) {
                if (field.field_hash == field_hash &&
                    (field.value_type == 4 || field.value_type == 6 ||
                     field.value_type == 7) && field.raw_value != 0) {
                    unique.try_emplace(field.raw_value, field.value);
                }
            }
        };
        add(entry.second.core_fields);
        add(entry.second.combat_fields);
    }
    std::vector<std::pair<uint32_t, std::string>> out(unique.begin(),
                                                       unique.end());
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
        return a.second < b.second;
    });
    return out;
}

void draw_npc_value_field(NpcAuthoring::FieldValue& field) {
    ImGui::PushID(static_cast<int>(field.field_hash));
    if (field.value_type == 0) {
        bool value = field.raw_value != 0;
        if (ImGui::Checkbox(field.label.c_str(), &value)) {
            field.raw_value = value ? 1u : 0u;
            field.display_value = value ? "Yes" : "No";
        }
    } else if (field.value_type == 1 || field.value_type == 5) {
        int value = static_cast<int32_t>(field.raw_value);
        if (ImGui::InputInt(field.label.c_str(), &value)) {
            field.raw_value = static_cast<uint32_t>(value);
            field.display_value = std::to_string(value);
        }
    } else if (field.value_type == 3) {
        float value = 0.0f;
        std::memcpy(&value, &field.raw_value, sizeof(value));
        if (ImGui::InputFloat(field.label.c_str(), &value, 0.0f, 0.0f,
                              "%.3f")) {
            std::memcpy(&field.raw_value, &value, sizeof(value));
            char buffer[48];
            std::snprintf(buffer, sizeof(buffer), "%.3f", value);
            field.display_value = buffer;
        }
    } else if (field.value_type == 4 || field.value_type == 6 ||
               field.value_type == 7) {
        std::vector<std::pair<uint32_t, std::string>> options =
            npc_reference_options(field.field_hash);
        std::string preview = field.display_value;
        if (preview.empty()) {
            char buffer[16];
            std::snprintf(buffer, sizeof(buffer), "0x%08X",
                          field.raw_value);
            preview = buffer;
        }
        if (ImGui::BeginCombo(field.label.c_str(), preview.c_str())) {
            for (const auto& option : options) {
                const bool selected = option.first == field.raw_value;
                if (ImGui::Selectable(option.second.c_str(), selected)) {
                    field.raw_value = option.first;
                    field.display_value = option.second;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    } else {
        ImGui::Text("%s: %s", field.label.c_str(),
                    field.display_value.c_str());
    }
    ImGui::PopID();
}

