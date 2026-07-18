bool apply_chest_contents(GdbEdit::GdbFile& g,
                          uint32_t entity_hash,
                          const std::vector<uint32_t>& items,
                          uint32_t loot_table_record,
                          bool replace_loot,
                          std::string& err)
{
    constexpr uint32_t kParent = 0x5F6317D5u;
    constexpr uint32_t kNull = 0x811C9DC5u;
    constexpr uint32_t kInvCompA = 0x1C7D7B74u;
    constexpr uint32_t kInvCompB = 0x73AB8B6Au;
    constexpr uint32_t kInitialItems = 0x9C24A50Du;
    constexpr uint32_t kChestInventoryBase = 0xB5E7A074u;
    constexpr uint32_t kEmptyInitialItemsBase = 0xBE56F154u;
    constexpr uint32_t kItemRepopulationBase = 0x088F64E7u;

    if (g.FindRecord(entity_hash) < 0) {
        err = "entity record not in level gdb";
        return false;
    }

    std::vector<GdbEdit::Field> fields;
    fields.reserve(items.size() + 1);
    std::unordered_set<uint32_t> used{kParent};
    GdbEdit::Field parent_field;
    parent_field.hash = kParent;
    parent_field.type = 6;
    parent_field.value = kEmptyInitialItemsBase;
    parent_field.decl = 0;
    fields.push_back(parent_field);
    for (size_t i = 0; i < items.size(); ++i) {
        char name[32];
        std::snprintf(name, sizeof(name), "F2ABItem%zu", i);
        uint32_t fh = 0x811C9DC5u;
        for (const char* p = name; *p; ++p) {
            fh *= 0x01000193u;
            fh ^= uint32_t(uint8_t(*p));
        }
        while (!used.insert(fh).second) ++fh;
        GdbEdit::Field f;
        f.hash = fh;
        f.type = 7;
        f.value = items[i];
        f.decl = uint32_t(i + 1);
        fields.push_back(f);
    }
    const uint32_t list_hash = g.AllocRecordHash();
    if (!g.AddRecord(list_hash, std::move(fields), 1)) {
        err = "list record append failed";
        return false;
    }

    GdbEdit::Field f;
    uint32_t inv_hash = 0;
    uint32_t inv_field_hash = kInvCompA;
    bool have_local_field = false;
    if (g.FindLocalField(entity_hash, kInvCompA, f)) {
        have_local_field = true;
        inv_field_hash = kInvCompA;
    } else if (g.FindLocalField(entity_hash, kInvCompB, f)) {
        have_local_field = true;
        inv_field_hash = kInvCompB;
    }
    if (have_local_field && f.value != 0 && f.value != kNull &&
        g.FindRecord(f.value) >= 0) {
        inv_hash = f.value;
    } else {

        uint32_t inherit =
            (have_local_field && f.value != 0 && f.value != kNull)
                ? f.value
                : 0;
        if (!inherit) inherit = kChestInventoryBase;
        inv_hash = g.AllocRecordHash();
        std::vector<GdbEdit::Field> fs;
        if (inherit) {
            GdbEdit::Field pf;
            pf.hash = kParent;
            pf.type = 6;
            pf.value = inherit;
            pf.decl = 0;
            fs.push_back(pf);
        }
        if (!g.AddRecord(inv_hash, fs, 1)) {
            err = "inventory record append failed";
            return false;
        }
        if (have_local_field) {
            if (!g.SetFieldValue(entity_hash, inv_field_hash, inv_hash)) {
                err = "inventory field rewrite failed";
                return false;
            }
        } else if (!g.AddField(entity_hash, kInvCompA, 6, inv_hash, 1)) {
            err = "inventory field append failed";
            return false;
        }
    }

    GdbEdit::Field items_field;
    if (g.FindLocalField(inv_hash, kInitialItems, items_field)) {
        if (!g.SetFieldValue(inv_hash, kInitialItems, list_hash)) {
            err = "InitialItems rewrite failed";
            return false;
        }
    } else if (!g.AddField(inv_hash, kInitialItems, 6, list_hash, 1)) {
        err = "InitialItems append failed";
        return false;
    }

    if (replace_loot) {
        constexpr uint32_t kItemRepopulationData = 0xFDF2E63Au;
        if (loot_table_record != 0) {
            constexpr uint32_t kPotentialItems = 0x4FB47937u;
            constexpr uint32_t kChanceOfRespawning = 0x993B9AA2u;
            const uint32_t repop_hash = g.AllocRecordHash();
            std::vector<GdbEdit::Field> rf;
            GdbEdit::Field f2;
            f2.hash = kParent;
            f2.type = 6;
            f2.value = kItemRepopulationBase;
            f2.decl = 0;
            rf.push_back(f2);
            f2.hash = kPotentialItems;
            f2.type = 6;
            f2.value = loot_table_record;
            f2.decl = 1;
            rf.push_back(f2);
            f2.hash = kChanceOfRespawning;
            f2.type = 3;
            f2.decl = 2;
            const float chance = 1.0f;
            std::memcpy(&f2.value, &chance, 4);
            rf.push_back(f2);
            if (!g.AddRecord(repop_hash, rf, 1)) {
                err = "repopulation record append failed";
                return false;
            }
            GdbEdit::Field rp;
            if (g.FindLocalField(inv_hash, kItemRepopulationData, rp)) {
                if (!g.SetFieldValue(inv_hash, kItemRepopulationData,
                                     repop_hash)) {
                    err = "ItemRepopulationData rewrite failed";
                    return false;
                }
            } else if (!g.AddField(inv_hash, kItemRepopulationData, 6,
                                   repop_hash, 2)) {
                err = "ItemRepopulationData append failed";
                return false;
            }
        } else {
            GdbEdit::Field rp;
            if (g.FindLocalField(inv_hash, kItemRepopulationData, rp)) {
                if (!g.SetFieldValue(inv_hash, kItemRepopulationData,
                                     kNull)) {
                    err = "ItemRepopulationData clear failed";
                    return false;
                }
            } else if (!g.AddField(inv_hash, kItemRepopulationData, 6,
                                   kNull, 2)) {
                err = "ItemRepopulationData clear append failed";
                return false;
            }
        }
    }
    return true;
}

