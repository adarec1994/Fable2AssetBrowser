constexpr uint32_t kHashChestComponent        = 0x379C25A9;
constexpr uint32_t kHashSilverKeysNeeded      = 0xB208E419;
constexpr uint32_t kHashDiggingSpotComponent  = 0x4D89E63B;
constexpr uint32_t kHashDiveSpotComponent     = 0x76E2C0A0;
constexpr uint32_t kHashDogLeadToComponent    = 0x09D8D1BD;
constexpr uint32_t kHashDogLeadPriority       = 0x6C62C3DD;
constexpr uint32_t kHashDogLeadRadius         = 0xD422BC6F;
constexpr uint32_t kHashInventoryComponentA   = 0x1C7D7B74;
constexpr uint32_t kHashInventoryComponentB   = 0x73AB8B6A;
constexpr uint32_t kHashInitialItems          = 0x9C24A50D;
constexpr uint32_t kHashItemRepopulationData  = 0xFDF2E63A;
constexpr uint32_t kHashPotentialItems        = 0x4FB47937;
constexpr uint32_t kHashChanceOfRespawning    = 0x993B9AA2;
constexpr uint32_t kHashCanBeStolenFrom       = 0xFDC33317;
constexpr uint32_t kHashCanRespawnItemsOnce   = 0xC19C6EE2;
constexpr uint32_t kHashCanRespawnRepeatedly  = 0x6C5206D2;
constexpr uint32_t kHashItemReference         = 0x32A71597;
constexpr uint32_t kHashLootWeight            = 0x04C07DF9;
constexpr uint32_t kHashMinFurnitureRating    = 0xCCF9B9CC;
constexpr uint32_t kHashMaxFurnitureRating    = 0x46976DB6;
constexpr uint32_t kHashMinChapterProgress    = 0xABD2D73B;
constexpr uint32_t kHashMaxChapterProgress    = 0x7CC73E81;
constexpr uint32_t kHashInventoryItemComponent = 0xC3318103;
constexpr uint32_t kHashNameTag               = 0x9555A6FC;
constexpr uint32_t kHashMoneyComponent        = 0xE21AB7A0;
constexpr uint32_t kHashMoney                 = 0x941C7FA7;

constexpr uint32_t kHashCreatureComponent     = 0xC3B90D4F;
constexpr uint32_t kHashHealthComponent       = 0x26546FBC;
constexpr uint32_t kHashCombatComponent       = 0xF6D5AD36;
constexpr uint32_t kHashFactionComponent      = 0x8CE69EBC;
constexpr uint32_t kHashFaction               = 0x6F3C0103;
constexpr uint32_t kHashCombatBalanceParams   = 0x48D1A921;
constexpr uint32_t kHashCombatBalanceTable    = 0x1754A434;

constexpr uint32_t kHashRandomlyGeneratedItem = 0x9121DCA1;
constexpr uint32_t kHashRandomTableEntry      = 0x951C5AA5;

struct MultiGdbCursor {
    const GdbView* view = nullptr;
    size_t record = 0;
};

using MultiGdbRecordIndex =
    std::unordered_map<uint32_t, MultiGdbCursor>;

MultiGdbRecordIndex BuildMultiGdbRecordIndex(
    const std::vector<const GdbView*>& views)
{
    size_t record_count = 0;
    for (const GdbView* view : views) {
        if (view) record_count += view->count;
    }
    MultiGdbRecordIndex index;
    index.reserve(record_count * 2 + 1);
    for (const GdbView* view : views) {
        if (!view || !view->ok) continue;
        const size_t count = std::min<size_t>(
            view->count, view->record_data_offsets.size());
        for (size_t i = 0; i < count; ++i) {
            const uint32_t hash = ReadBeU32(
                view->bytes.data() + view->hash_base + i * 4);
            if (hash == 0 || hash == kHashNull) continue;
            index.try_emplace(
                hash, MultiGdbCursor{view, view->record_data_offsets[i]});
        }
    }
    return index;
}

bool MultiLookup(const MultiGdbRecordIndex& index,
                 uint32_t hash,
                 MultiGdbCursor& out)
{
    if (hash == 0 || hash == kHashNull) return false;
    const auto found = index.find(hash);
    if (found == index.end()) return false;
    out = found->second;
    return true;
}

bool MultiFindInherited(const MultiGdbRecordIndex& index,
                        MultiGdbCursor cur,
                        uint32_t field_hash,
                        uint8_t expected_type,
                        MultiGdbCursor& out_owner,
                        uint32_t& out_value,
                        uint8_t* out_type = nullptr)
{
    std::unordered_set<uint32_t> visited;
    for (int depth = 0; depth < 64; ++depth) {
        size_t slot = 0;
        uint8_t type = 0;
        if (cur.view->findLocal(cur.record, field_hash, expected_type,
                                slot, &type)) {
            out_owner = cur;
            out_value = ReadBeU32(cur.view->bytes.data() + slot);
            if (out_type) *out_type = type;
            return true;
        }
        size_t parent_slot = 0;
        if (!cur.view->findLocal(cur.record, kHashParent, 6,
                                 parent_slot, nullptr)) {
            return false;
        }
        const uint32_t parent_hash =
            ReadBeU32(cur.view->bytes.data() + parent_slot);
        if (!visited.insert(parent_hash).second) return false;
        MultiGdbCursor next;
        if (!MultiLookup(index, parent_hash, next)) return false;
        cur = next;
    }
    return false;
}

bool MultiLookup(const std::vector<const GdbView*>& views,
                 uint32_t hash,
                 MultiGdbCursor& out)
{
    if (hash == 0 || hash == kHashNull) return false;
    for (const GdbView* v : views) {
        size_t rec = 0;
        if (v->lookup(hash, rec)) {
            out.view = v;
            out.record = rec;
            return true;
        }
    }
    return false;
}

bool MultiFindInherited(const std::vector<const GdbView*>& views,
                        MultiGdbCursor cur,
                        uint32_t field_hash,
                        uint8_t expected_type,
                        MultiGdbCursor& out_owner,
                        uint32_t& out_value,
                        uint8_t* out_type = nullptr)
{
    for (int depth = 0; depth < 64; ++depth) {
        size_t slot = 0;
        uint8_t type = 0;
        if (cur.view->findLocal(cur.record, field_hash, expected_type,
                                slot, &type)) {
            out_owner = cur;
            out_value = ReadBeU32(cur.view->bytes.data() + slot);
            if (out_type) *out_type = type;
            return true;
        }
        size_t parent_slot = 0;
        if (!cur.view->findLocal(cur.record, kHashParent, 6,
                                 parent_slot, nullptr)) {
            return false;
        }
        const uint32_t parent_hash =
            ReadBeU32(cur.view->bytes.data() + parent_slot);
        MultiGdbCursor next;
        if (!MultiLookup(views, parent_hash, next)) return false;
        if (next.view == cur.view && next.record == cur.record) return false;
        cur = next;
    }
    return false;
}

bool MultiReadInheritedFloat(const std::vector<const GdbView*>& views,
                             const MultiGdbCursor& cur,
                             uint32_t field_hash,
                             float& out_value)
{
    MultiGdbCursor owner;
    uint32_t raw = 0;
    if (!MultiFindInherited(views, cur, field_hash, 3, owner, raw)) {
        return false;
    }

    std::memcpy(&out_value, &raw, 4);
    return std::isfinite(out_value);
}

bool MultiReadInheritedBool(const std::vector<const GdbView*>& views,
                            const MultiGdbCursor& cur,
                            uint32_t field_hash,
                            int& out_value)
{
    MultiGdbCursor owner;
    uint32_t raw = 0;
    uint8_t type = 0;
    if (!MultiFindInherited(views, cur, field_hash, 0xFF,
                            owner, raw, &type) || type != 0) {
        return false;
    }
    out_value = raw != 0 ? 1 : 0;
    return true;
}

void ReadContentsItemInfo(const std::vector<const GdbView*>& views,
                          uint32_t item_hash,
                          EntityContentsItem& item,
                          const std::unordered_map<uint32_t, std::string>*
                              dict = nullptr)
{
    auto lookup_name = [&](uint32_t h) -> std::string {
        if (dict) {
            auto it = dict->find(h);
            if (it != dict->end() && !it->second.empty()) {
                return it->second;
            }
        }
        return GdbHashName(h, {});
    };

    item.record_hash = item_hash;
    MultiGdbCursor rec;
    if (!MultiLookup(views, item_hash, rec)) return;

    MultiGdbCursor comp_field_owner;
    uint32_t comp_hash = 0;
    if (MultiFindInherited(views, rec, kHashInventoryItemComponent, 6,
                           comp_field_owner, comp_hash)) {
        MultiGdbCursor comp;
        if (MultiLookup(views, comp_hash, comp)) {
            MultiGdbCursor tag_owner;
            uint32_t tag_hash = 0;
            uint8_t tag_type = 0;
            if (MultiFindInherited(views, comp, kHashNameTag, 0xFF,
                                   tag_owner, tag_hash, &tag_type) &&
                (tag_type == 4 || tag_type == 7) &&
                tag_hash != 0 && tag_hash != kHashNull) {
                item.name_tag = lookup_name(tag_hash);
                item.name_tag_hash = tag_hash;
            }
        }
    }
    if (MultiFindInherited(views, rec, kHashMoneyComponent, 6,
                           comp_field_owner, comp_hash)) {
        MultiGdbCursor comp;
        if (MultiLookup(views, comp_hash, comp)) {
            MultiGdbCursor money_owner;
            uint32_t money_raw = 0;
            uint8_t money_type = 0;
            if (MultiFindInherited(views, comp, kHashMoney, 0xFF,
                                   money_owner, money_raw, &money_type) &&
                (money_type == 1 || money_type == 5)) {
                item.money = int(money_raw);
            }
        }
    }

    if (item.name_tag.empty() &&
        MultiFindInherited(views, rec, kHashRandomlyGeneratedItem, 6,
                           comp_field_owner, comp_hash)) {
        MultiGdbCursor comp;
        if (MultiLookup(views, comp_hash, comp)) {
            MultiGdbCursor table_owner;
            uint32_t table_hash = 0;
            uint8_t table_type = 0;
            if (MultiFindInherited(views, comp, kHashRandomTableEntry, 0xFF,
                                   table_owner, table_hash, &table_type) &&
                (table_type == 4 || table_type == 7) &&
                table_hash != 0 && table_hash != kHashNull) {
                const std::string table_name = lookup_name(table_hash);
                if (!table_name.empty()) {
                    item.name_tag = "Random (" + table_name + ")";
                }
            }
        }
    }

    if (item.name_tag.empty()) {
        item.name_tag = lookup_name(item_hash);
    }
}
