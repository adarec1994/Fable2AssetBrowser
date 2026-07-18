static bool level_marker_visible(const LevelSpawnMarker& marker)
{
    if (::is_player_start_marker(marker)) return true;
    if (marker.kind == 0) return false;
    if (marker.kind == 4) return S.show_dig_spots;
    if (marker.is_container && S.show_containers) return true;
    if (marker.kind == 3) return S.show_ent_npcs;
    if (marker.kind == 6) return S.show_entity_models;
    if (marker.kind == 5) return false;
    return S.show_spawn_markers;
}

static std::string pretty_container_item_tag(std::string tag, int money)
{
    for (const char* pfx : {"INV_ITEM_", "OBJECT_", "TEXT_"}) {
        const size_t n = std::strlen(pfx);
        if (tag.size() > n && tag.compare(0, n, pfx) == 0) {
            tag = tag.substr(n);
            break;
        }
    }
    if (tag.size() > 5 &&
        tag.compare(tag.size() - 5, 5, "_NAME") == 0) {
        tag.resize(tag.size() - 5);
    }
    if (tag.find('_') != std::string::npos ||
        std::none_of(tag.begin(), tag.end(), [](unsigned char c) {
            return std::islower(c);
        })) {
        bool word_start = true;
        for (char& c : tag) {
            if (c == '_') {
                c = ' ';
                word_start = true;
            } else {
                c = word_start
                    ? char(std::toupper(static_cast<unsigned char>(c)))
                    : char(std::tolower(static_cast<unsigned char>(c)));
                word_start = false;
            }
        }
    }
    if (money >= 0) {
        tag += " (" + std::to_string(money) + " gold)";
    }
    return tag;
}

static std::string container_catalog_label(uint32_t record_hash)
{
    for (const auto& item : g_item_details) {
        if (item.record_hash == record_hash && !item.display_name.empty()) {
            return item.display_name;
        }
    }
    for (const auto& item : g_level_item_catalog) {
        if (item.record_hash == record_hash) {
            return pretty_container_item_tag(item.label, item.money);
        }
    }
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%08X", record_hash);
    return buf;
}

static std::string container_item_label(const Gdb::EntityContentsItem& item)
{
    if (!item.display_name.empty()) return item.display_name;
    std::string tag = !item.name_tag.empty() ? item.name_tag
                                             : item.entry_label;
    if (tag.empty()) return container_catalog_label(item.record_hash);
    return pretty_container_item_tag(std::move(tag), item.money);
}

static const char* container_yes_no_unknown(int value)
{
    if (value < 0) return "Not authored";
    return value != 0 ? "Yes" : "No";
}
