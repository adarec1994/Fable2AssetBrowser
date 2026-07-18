static void add_quest_item_to_container(uint32_t entity_hash,
                                        uint32_t item_hash)
{
    if (!entity_hash || !item_hash) return;
    std::vector<uint32_t> items;
    if (!LevelEdit::GetChestContents(entity_hash, items)) {
        const auto existing = g_level_entity_contents.find(entity_hash);
        if (existing != g_level_entity_contents.end()) {
            for (const Gdb::EntityContentsItem& item :
                 existing->second.initial_items) {
                items.push_back(item.record_hash);
            }
        }
    }
    if (std::find(items.begin(), items.end(), item_hash) == items.end()) {
        items.push_back(item_hash);
        LevelEdit::SetChestContents(entity_hash, items);
    }
}
