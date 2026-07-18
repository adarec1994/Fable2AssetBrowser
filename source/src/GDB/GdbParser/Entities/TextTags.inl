std::unordered_map<uint32_t, EntityTextTags> ExtractEntityTextTags(
    const std::vector<const std::vector<uint8_t>*>& gdbs,
    const std::vector<std::pair<uint32_t, std::string>>& hash_to_name)
{
    std::unordered_map<uint32_t, EntityTextTags> out;
    constexpr uint32_t kHashTextTags = 0x709D872Bu;
    constexpr uint32_t kTagFields[5] = {
        0x1D280BA4u,
        0x1D280BA7u,
        0x1D280BA6u,
        0x1D280BA1u,
        0x1D280BA0u,
    };

    std::vector<std::unique_ptr<GdbView>> owned;
    std::vector<const GdbView*> views;
    for (const auto* g : gdbs) {
        if (!g || g->empty()) continue;
        auto v = std::make_unique<GdbView>(*g);
        if (v->ok) {
            views.push_back(v.get());
            owned.push_back(std::move(v));
        }
    }
    if (views.empty() || hash_to_name.empty()) return out;
    const GdbView* level_view = views.front();

    constexpr uint32_t kHashReadableComponent = 0x89ABB47Eu;
    constexpr uint32_t kHashTextTag = 0xB8F45248u;

    for (const auto& kv : hash_to_name) {
        const uint32_t entity_hash = kv.first;
        MultiGdbCursor ent;
        if (!MultiLookup(views, entity_hash, ent)) continue;

        EntityTextTags et;

        {
            MultiGdbCursor rc_owner;
            uint32_t rc_hash = 0;
            if (MultiFindInherited(views, ent, kHashReadableComponent, 6,
                                   rc_owner, rc_hash) &&
                rc_hash != 0 && rc_hash != kHashNull) {
                MultiGdbCursor rc;
                if (MultiLookup(views, rc_hash, rc)) {
                    MultiGdbCursor owner;
                    uint32_t th = 0;
                    uint8_t ty = 0;
                    if (MultiFindInherited(views, rc, kHashTextTag, 0xFF,
                                           owner, th, &ty) &&
                        (ty == 4 || ty == 7) && th != 0 &&
                        th != kHashNull) {
                        et.tags_record_hash = rc_hash;
                        et.tags_record_in_level_gdb =
                            (rc.view == level_view);
                        et.tag_hashes.push_back(th);
                    }
                }
            }
        }

        if (et.tag_hashes.empty()) {
            MultiGdbCursor tt_owner;
            uint32_t tags_hash = 0;
            if (MultiFindInherited(views, ent, kHashTextTags, 6,
                                   tt_owner, tags_hash) &&
                tags_hash != 0 && tags_hash != kHashNull) {
                MultiGdbCursor tags;
                if (MultiLookup(views, tags_hash, tags)) {
                    et.tags_record_hash = tags_hash;
                    et.tags_record_in_level_gdb =
                        (tags.view == level_view);
                    for (uint32_t tf : kTagFields) {
                        MultiGdbCursor owner;
                        uint32_t th = 0;
                        uint8_t ty = 0;
                        if (MultiFindInherited(views, tags, tf, 0xFF,
                                               owner, th, &ty) &&
                            (ty == 4 || ty == 7) && th != 0 &&
                            th != kHashNull) {
                            et.tag_hashes.push_back(th);
                        }
                    }
                }
            }
        }

        if (et.tag_hashes.empty()) {
            constexpr uint32_t kHashNameTag = 0x9555A6FCu;
            constexpr uint32_t kHashDescriptionTag = 0xD823B12Bu;
            constexpr uint32_t kHashInventoryItemComponent = 0xC3318103u;
            constexpr uint32_t kHashCreatureComponent = 0xC3B90D4Fu;
            MultiGdbCursor tag_scopes[3];
            int n_scopes = 0;
            tag_scopes[n_scopes++] = ent;
            for (uint32_t cf : {kHashInventoryItemComponent,
                                kHashCreatureComponent}) {
                MultiGdbCursor owner;
                uint32_t ch = 0;
                if (MultiFindInherited(views, ent, cf, 6, owner, ch) &&
                    ch != 0 && ch != kHashNull) {
                    MultiGdbCursor comp;
                    if (MultiLookup(views, ch, comp) && n_scopes < 3) {
                        tag_scopes[n_scopes++] = comp;
                    }
                }
            }
            for (int si = 0; si < n_scopes && et.tag_hashes.empty();
                 ++si) {
                for (uint32_t tf : {kHashNameTag, kHashDescriptionTag}) {
                    MultiGdbCursor owner;
                    uint32_t th = 0;
                    uint8_t ty = 0;
                    if (MultiFindInherited(views, tag_scopes[si], tf,
                                           0xFF, owner, th, &ty) &&
                        (ty == 4 || ty == 7) && th != 0 &&
                        th != kHashNull) {
                        et.tag_hashes.push_back(th);
                    }
                }
            }
        }
        if (!et.tag_hashes.empty()) {
            float x = 0, y = 0, z = 0, rx = 0, ry = 0, rz = 0;
            bool hr = false;
            if (ent.view &&
                (TryComponentTransformRecord(*ent.view, ent.record, x, y,
                                             z, rx, ry, rz, hr) ||
                 TryTransformRecord(*ent.view, ent.record, x, y, z, rx,
                                    ry, rz, hr))) {
                et.has_pos = true;
                et.x = x;
                et.y = y;
                et.z = z;
            }
            out.emplace(entity_hash, std::move(et));
        }
    }

    for (uint32_t i = 0; i < level_view->count; ++i) {
        if (i >= level_view->record_data_offsets.size()) break;
        const uint32_t rh = ReadBeU32(level_view->bytes.data() +
                                      level_view->hash_base +
                                      size_t(i) * 4);
        if (out.count(rh)) continue;
        const size_t rec = level_view->record_data_offsets[i];

        constexpr uint32_t kSimpleTransform = 0x619F96CFu;
        float x = 0, y = 0, z = 0, rx = 0, ry = 0, rz = 0;
        bool hr = false;
        const bool got_pos =
            TryComponentTransformRecord(*level_view, rec, x, y, z, rx,
                                        ry, rz, hr) ||
            TryComponentTransformField(*level_view, rec,
                                       kSimpleTransform, x, y, z, rx,
                                       ry, rz, hr) ||
            TryTransformRecord(*level_view, rec, x, y, z, rx, ry, rz,
                               hr);

        MultiGdbCursor cur{level_view, rec};
        EntityTextTags et;
        MultiGdbCursor rc_owner;
        uint32_t rc_hash = 0;
        if (MultiFindInherited(views, cur, kHashReadableComponent, 6,
                               rc_owner, rc_hash) &&
            rc_hash != 0 && rc_hash != kHashNull) {
            MultiGdbCursor rc;
            MultiGdbCursor owner;
            uint32_t th = 0;
            uint8_t ty = 0;
            if (MultiLookup(views, rc_hash, rc) &&
                MultiFindInherited(views, rc, kHashTextTag, 0xFF, owner,
                                   th, &ty) &&
                (ty == 4 || ty == 7) && th != 0 && th != kHashNull) {
                et.tags_record_hash = rc_hash;
                et.tag_hashes.push_back(th);
            }
        }
        if (et.tag_hashes.empty()) {
            constexpr uint32_t kHashInventoryItemComponent = 0xC3318103u;
            constexpr uint32_t kHashNameTag = 0x9555A6FCu;
            constexpr uint32_t kHashDescriptionTag = 0xD823B12Bu;
            MultiGdbCursor io;
            uint32_t ih = 0;
            if (MultiFindInherited(views, cur,
                                   kHashInventoryItemComponent, 6, io,
                                   ih) &&
                ih != 0 && ih != kHashNull) {
                MultiGdbCursor item;
                if (MultiLookup(views, ih, item)) {
                    for (uint32_t tf : {kHashNameTag,
                                        kHashDescriptionTag}) {
                        MultiGdbCursor owner;
                        uint32_t th = 0;
                        uint8_t ty = 0;
                        if (MultiFindInherited(views, item, tf, 0xFF,
                                               owner, th, &ty) &&
                            (ty == 4 || ty == 7) && th != 0 &&
                            th != kHashNull) {
                            et.tags_record_hash = ih;
                            et.tag_hashes.push_back(th);
                        }
                    }
                }
            }
        }
        if (et.tag_hashes.empty()) continue;
        et.tags_record_in_level_gdb = true;
        et.has_pos = got_pos;
        et.x = x;
        et.y = y;
        et.z = z;
        out.emplace(rh, std::move(et));
    }
    return out;
}
