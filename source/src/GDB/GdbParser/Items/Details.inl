std::vector<ItemDetail> BuildItemDetails(
    const std::vector<const std::vector<uint8_t>*>& gdbs)
{
    std::vector<ItemDetail> out;
    constexpr uint32_t kNameTag = 0x9555A6FCu;
    constexpr uint32_t kDescTag = 0xD823B12Bu;
    constexpr uint32_t kIconGraphic = 0x46F0F9CEu;

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
    if (views.empty()) return out;

    std::unordered_map<uint32_t, std::string> dict;
    for (const auto* g : gdbs) {
        if (!g || g->empty()) continue;
        auto d = LoadEmbeddedDict(*g);
        dict.insert(d.begin(), d.end());
    }
    auto dict_name = [&](uint32_t h) -> std::string {
        auto it = dict.find(h);
        return it != dict.end() ? it->second : std::string();
    };

    auto collect_name_list = [&](uint32_t list_hash) {
        std::vector<std::string> names;
        MultiGdbCursor walk;
        if (!MultiLookup(views, list_hash, walk)) return names;
        std::unordered_set<uint32_t> inherited_slots;
        std::unordered_set<uint32_t> visited_records;
        for (int depth = 0; depth < 64; ++depth) {
            size_t schema = 0;
            uint32_t field_count = 0;
            if (!walk.view->schema(walk.record, schema, field_count)) break;
            const size_t hashes = schema + 4;
            const size_t descriptors = hashes + size_t(field_count) * 4;
            std::unordered_set<uint32_t> local_slots;
            for (uint32_t field = 0; field < field_count; ++field) {
                const uint32_t field_hash = ReadBeU32(
                    walk.view->bytes.data() + hashes + size_t(field) * 4);
                if (field_hash == kHashParent ||
                    inherited_slots.find(field_hash) != inherited_slots.end()) {
                    continue;
                }
                local_slots.insert(field_hash);
                const uint32_t descriptor = ReadBeU32(
                    walk.view->bytes.data() + descriptors + size_t(field) * 4);
                if (uint8_t(descriptor >> 24) != 5) continue;
                const size_t value_slot = walk.record + 4 + size_t(field) * 4;
                if (value_slot + 4 > walk.view->body_end) continue;
                const uint32_t value = ReadBeU32(
                    walk.view->bytes.data() + value_slot);
                if (value == 0 || value == kHashNull) continue;
                std::string name = dict_name(value);
                if (!name.empty()) names.push_back(std::move(name));
            }
            inherited_slots.insert(local_slots.begin(), local_slots.end());

            size_t parent_slot = 0;
            if (!walk.view->findLocal(walk.record, kHashParent, 6,
                                      parent_slot, nullptr)) break;
            const uint32_t parent = ReadBeU32(
                walk.view->bytes.data() + parent_slot);
            if (parent == 0 || parent == kHashNull ||
                !visited_records.insert(parent).second) break;
            MultiGdbCursor next;
            if (!MultiLookup(views, parent, next)) break;
            walk = next;
        }
        return names;
    };

    auto collect_bitmask = [&](uint32_t list_hash) {
        uint32_t mask = 0;
        MultiGdbCursor walk;
        if (!MultiLookup(views, list_hash, walk)) return mask;
        std::unordered_set<uint32_t> inherited_slots;
        std::unordered_set<uint32_t> visited_records;
        for (int depth = 0; depth < 64; ++depth) {
            size_t schema = 0;
            uint32_t field_count = 0;
            if (!walk.view->schema(walk.record, schema, field_count)) break;
            const size_t hashes = schema + 4;
            const size_t descriptors = hashes + size_t(field_count) * 4;
            std::unordered_set<uint32_t> local_slots;
            for (uint32_t field = 0; field < field_count; ++field) {
                const uint32_t field_hash = ReadBeU32(
                    walk.view->bytes.data() + hashes + size_t(field) * 4);
                if (field_hash == kHashParent ||
                    inherited_slots.find(field_hash) != inherited_slots.end()) {
                    continue;
                }
                local_slots.insert(field_hash);
                const uint32_t descriptor = ReadBeU32(
                    walk.view->bytes.data() + descriptors + size_t(field) * 4);
                if (uint8_t(descriptor >> 24) != 5) continue;
                const size_t value_slot = walk.record + 4 + size_t(field) * 4;
                if (value_slot + 4 <= walk.view->body_end) {
                    mask |= ReadBeU32(walk.view->bytes.data() + value_slot);
                }
            }
            inherited_slots.insert(local_slots.begin(), local_slots.end());
            size_t parent_slot = 0;
            if (!walk.view->findLocal(walk.record, kHashParent, 6,
                                      parent_slot, nullptr)) break;
            const uint32_t parent = ReadBeU32(
                walk.view->bytes.data() + parent_slot);
            if (parent == 0 || parent == kHashNull ||
                !visited_records.insert(parent).second) break;
            MultiGdbCursor next;
            if (!MultiLookup(views, parent, next)) break;
            walk = next;
        }
        return mask;
    };

    auto is_stat_field = [&](const std::string& nm) {
        static const char* kSkip[] = {
            "GUIScreen", "Icon", "parent", "Component", "Tag",
            "Offset", "Rotation", "Translation", "Mesh", "Model",
            "Texture", "ScriptName", "SoundEvent", "Expression",
        };
        for (const char* s : kSkip) {
            if (nm.find(s) != std::string::npos) return false;
        }
        return true;
    };
    constexpr uint32_t kAugmentable = 0x9DC93C5Bu;
    constexpr uint32_t kAugSlots[] = {
        0xFCF3EB8Eu, 0xFCF3EB8Du, 0xFCF3EB8Cu, 0xFCF3EB8Bu,
        0xFCF3EB8Au, 0xFCF3EB89u, 0xFCF3EB88u, 0xFCF3EB87u,
        0xFCF3EB86u,
    };

    std::unordered_set<uint32_t> seen;
    for (const GdbView* vw : views) {
        const GdbView& view = *vw;
        for (uint32_t i = 0; i < view.count; ++i) {
            if (i >= view.record_data_offsets.size()) break;
            const size_t rec = view.record_data_offsets[i];








            size_t name_slot = 0;
            uint8_t name_ty = 0;
            const bool has_local_comp = view.findLocal(
                rec, kHashInventoryItemComponent, 6, name_slot, nullptr);
            bool is_inv_named = false;
            if (view.findLocal(rec, kNameTag, 0xFF, name_slot,
                               &name_ty) &&
                (name_ty == 4 || name_ty == 7)) {
                const uint32_t nh =
                    ReadBeU32(view.bytes.data() + name_slot);
                const std::string ns = dict_name(nh);
                is_inv_named = ns.compare(0, 9, "INV_ITEM_") == 0;
            }
            if (!has_local_comp && !is_inv_named) continue;

            const uint32_t rec_hash =
                ReadBeU32(view.bytes.data() + view.hash_base +
                          size_t(i) * 4);
            if (!seen.insert(rec_hash).second) continue;

            ItemDetail d;
            d.record_hash = rec_hash;
            d.internal_name = dict_name(rec_hash);

            EntityContentsItem info;
            ReadContentsItemInfo(views, rec_hash, info, &dict);
            d.money = info.money;



            {
                constexpr uint32_t kMoneyComponent = 0xE21AB7A0u;
                MultiGdbCursor mo;
                uint32_t mh = 0;
                MultiGdbCursor mc0{vw, rec};
                if (MultiFindInherited(views, mc0, kMoneyComponent, 6,
                                       mo, mh) &&
                    mh != 0 && mh != kHashNull) {
                    d.is_money = true;
                }
            }

            MultiGdbCursor cur{vw, rec};





            {
                constexpr uint32_t kAppearanceModifierComponent = 0xD949A6F5u;
                constexpr uint32_t kGraphicAppearanceStaticMeshComponent =
                    0x29CF50D1u;
                constexpr uint32_t kModelFile = 0x0C17DB4Eu;
                constexpr uint32_t kOverrideFemaleModel = 0xD71D2B14u;
                constexpr uint32_t kClustersCovered = 0x194C174Au;
                constexpr uint32_t kBodyAreasCovered = 0xB3ADD081u;
                constexpr uint32_t kOverrideFemaleClustersCovered =
                    0xF736AC4Au;
                constexpr uint32_t kClusterSortLayer = 0x88FD2918u;

                auto read_model = [&](const MultiGdbCursor& component,
                                      uint32_t field,
                                      uint32_t& hash,
                                      std::string& path) {
                    MultiGdbCursor owner;
                    uint32_t value = 0;
                    uint8_t type = 0;
                    if (MultiFindInherited(views, component, field, 0xFF,
                                           owner, value, &type) &&
                        (type == 4 || type == 7) && value != 0 &&
                        value != kHashNull) {
                        hash = value;
                        path = dict_name(value);
                    }
                };

                MultiGdbCursor owner;
                uint32_t component_hash = 0;
                if (MultiFindInherited(
                        views, cur, kGraphicAppearanceStaticMeshComponent, 6,
                        owner, component_hash) &&
                    component_hash != 0 && component_hash != kHashNull) {
                    MultiGdbCursor component;
                    if (MultiLookup(views, component_hash, component)) {
                        read_model(component, kModelFile,
                                   d.worn_model_path_hash,
                                   d.worn_model_path);
                    }
                }

                if (MultiFindInherited(views, cur,
                                       kAppearanceModifierComponent, 6,
                                       owner, component_hash) &&
                    component_hash != 0 && component_hash != kHashNull) {
                    MultiGdbCursor component;
                    if (MultiLookup(views, component_hash, component)) {
                        read_model(component, kOverrideFemaleModel,
                                   d.female_worn_model_path_hash,
                                   d.female_worn_model_path);

                        MultiGdbCursor field_owner;
                        uint32_t value = 0;
                        uint8_t type = 0;
                        if (MultiFindInherited(views, component,
                                               kClusterSortLayer, 0xFF,
                                               field_owner, value, &type) &&
                            (type == 1 || type == 5)) {
                            d.cluster_sort_layer = static_cast<int32_t>(value);
                        }
                        if (MultiFindInherited(views, component,
                                               kBodyAreasCovered, 6,
                                               field_owner, value) &&
                            value != 0 && value != kHashNull) {
                            d.body_areas_covered = collect_bitmask(value);
                        }
                        if (MultiFindInherited(views, component,
                                               kClustersCovered, 6,
                                               field_owner, value) &&
                            value != 0 && value != kHashNull) {
                            d.clusters_covered = collect_name_list(value);
                        }
                        if (MultiFindInherited(
                                views, component,
                                kOverrideFemaleClustersCovered, 6,
                                field_owner, value) &&
                            value != 0 && value != kHashNull) {
                            d.female_clusters_covered =
                                collect_name_list(value);
                        }
                    }
                }
            }


            MultiGdbCursor inv_comp;
            bool have_inv_comp = false;
            {
                MultiGdbCursor io;
                uint32_t ih = 0;
                if (MultiFindInherited(views, cur,
                                       kHashInventoryItemComponent, 6,
                                       io, ih) &&
                    ih != 0 && ih != kHashNull) {
                    have_inv_comp = MultiLookup(views, ih, inv_comp);
                }
            }





            {
                constexpr uint32_t kCategory = 0xFE6FDF41u;
                constexpr uint32_t kWeaponComponent = 0x2C34431Eu;
                constexpr uint32_t kWeaponType = 0xC7596B9Du;
                MultiGdbCursor owner;
                uint32_t value = 0;
                uint8_t type = 0;
                if (have_inv_comp &&
                    MultiFindInherited(views, inv_comp, kCategory, 0xFF,
                                       owner, value, &type) &&
                    (type == 1 || type == 5)) {
                    d.category = static_cast<int32_t>(value);
                }

                uint32_t weapon_component = 0;
                if (MultiFindInherited(views, cur, kWeaponComponent, 6,
                                       owner, weapon_component) &&
                    weapon_component != 0 &&
                    weapon_component != kHashNull) {
                    MultiGdbCursor component;
                    if (MultiLookup(views, weapon_component, component) &&
                        MultiFindInherited(views, component, kWeaponType,
                                           0xFF, owner, value, &type) &&
                        (type == 1 || type == 5)) {
                        d.weapon_type = static_cast<int32_t>(value);
                    }
                }
            }


            auto find_tag = [&](uint32_t field) -> uint32_t {
                MultiGdbCursor o;
                uint32_t v = 0;
                uint8_t t = 0;
                if (MultiFindInherited(views, cur, field, 0xFF, o, v,
                                       &t) &&
                    (t == 4 || t == 7) && v != kHashNull) {
                    return v;
                }
                if (have_inv_comp &&
                    MultiFindInherited(views, inv_comp, field, 0xFF, o,
                                       v, &t) &&
                    (t == 4 || t == 7) && v != kHashNull) {
                    return v;
                }
                return 0;
            };
            d.name_tag = find_tag(kNameTag);
            d.desc_tag = find_tag(kDescTag);
            const uint32_t icon = find_tag(kIconGraphic);
            if (icon) d.icon_tex = dict_name(icon);



            {
                MultiGdbCursor mw{vw, rec};
                for (int md = 0; md < 24 && !d.model_path_hash; ++md) {
                    const auto mh = CollectModelPathHashesForRecord(
                        *mw.view, mw.record);
                    if (!mh.empty()) {
                        d.model_path_hash = mh.front();
                        d.model_path = dict_name(mh.front());
                        break;
                    }
                    size_t pslot = 0;
                    if (!mw.view->findLocal(mw.record, kHashParent, 6,
                                            pslot, nullptr)) {
                        break;
                    }
                    const uint32_t ph =
                        ReadBeU32(mw.view->bytes.data() + pslot);
                    MultiGdbCursor nxt;
                    if (ph == 0 || ph == kHashNull ||
                        !MultiLookup(views, ph, nxt)) {
                        break;
                    }
                    mw = nxt;
                }
            }






            if (d.model_path.empty() && d.name_tag) {
                const std::string tag = dict_name(d.name_tag);
                static const std::string kWp = "INV_ITEM_WEAPON_";
                if (tag.compare(0, kWp.size(), kWp) == 0) {
                    std::string core = tag.substr(kWp.size());
                    const size_t np = core.rfind("_NAME");
                    if (np != std::string::npos) core.resize(np);
                    size_t bp;
                    while ((bp = core.find("BASE")) !=
                           std::string::npos) {
                        core.erase(bp, 4);
                    }
                    for (char& c : core) {
                        c = char(std::tolower((unsigned char)c));
                    }


                    if (core.find('_') != std::string::npos) {
                        d.model_path = core + ".mdl";
                    }
                }
            }

            std::string raw = dict_name(d.name_tag);
            if (raw.empty()) raw = dict_name(rec_hash);
            if (raw.empty()) raw = info.name_tag;
            if (!raw.empty()) {
                d.label = PrettifyTagLabel(raw);
            } else {
                char buf[24];
                std::snprintf(buf, sizeof(buf), "unnamed 0x%08X",
                              rec_hash);
                d.label = buf;
                d.unnamed = true;
            }

            std::unordered_set<uint32_t> stat_seen;


            auto collect_scalars = [&](MultiGdbCursor start) {
                MultiGdbCursor walk = start;
                for (int depth = 0;
                     depth < 24 && d.stats.size() < 40; ++depth) {
                    size_t sch = 0;
                    uint32_t nf = 0;
                    if (!walk.view->schema(walk.record, sch, nf)) break;
                    const size_t h0 = sch + 4;
                    const size_t d0 = h0 + size_t(nf) * 4;
                    for (uint32_t f = 0;
                         f < nf && d.stats.size() < 40; ++f) {
                        const uint32_t fh =
                            ReadBeU32(walk.view->bytes.data() + h0 +
                                      size_t(f) * 4);
                        if (fh == kHashParent) continue;
                        if (!stat_seen.insert(fh).second) continue;
                        const uint32_t desc =
                            ReadBeU32(walk.view->bytes.data() + d0 +
                                      size_t(f) * 4);
                        const uint8_t ft = uint8_t(desc >> 24);
                        if (ft != 0 && ft != 1 && ft != 3 && ft != 5) {
                            continue;
                        }
                        const std::string fn = dict_name(fh);
                        if (fn.empty() || !is_stat_field(fn)) continue;
                        const size_t vslot =
                            walk.record + 4 + size_t(f) * 4;
                        if (vslot + 4 > walk.view->body_end) continue;
                        const uint8_t* vp =
                            walk.view->bytes.data() + vslot;
                        char vb[48];
                        if (ft == 3) {
                            std::snprintf(vb, sizeof(vb), "%.3g",
                                          ReadBeF32(vp));
                        } else if (ft == 0) {
                            std::snprintf(vb, sizeof(vb), "%s",
                                          ReadBeU32(vp) ? "yes" : "no");
                        } else {
                            std::snprintf(vb, sizeof(vb), "%u",
                                          ReadBeU32(vp));
                        }
                        d.stats.emplace_back(fn, vb);
                    }
                    size_t pslot = 0;
                    if (!walk.view->findLocal(walk.record, kHashParent,
                                              6, pslot, nullptr)) {
                        break;
                    }
                    const uint32_t ph =
                        ReadBeU32(walk.view->bytes.data() + pslot);
                    MultiGdbCursor nxt;
                    if (ph == 0 || ph == kHashNull ||
                        !MultiLookup(views, ph, nxt)) {
                        break;
                    }
                    walk = nxt;
                }
            };


            collect_scalars(cur);


            constexpr uint32_t kStatComps[] = {
                0x2C34431Eu,
                0x0A644D42u,
                0xE21AB7A0u,
                0x6D04D9A2u,
            };
            for (uint32_t sc_hash : kStatComps) {
                MultiGdbCursor so;
                uint32_t comp = 0;
                if (MultiFindInherited(views, cur, sc_hash, 6, so,
                                       comp) &&
                    comp != 0 && comp != kHashNull) {
                    MultiGdbCursor cc;
                    if (MultiLookup(views, comp, cc)) {
                        collect_scalars(cc);
                    }
                }
            }


            {
                MultiGdbCursor ao;
                uint32_t aug_comp = 0;
                if (MultiFindInherited(views, cur, kAugmentable, 6, ao,
                                       aug_comp) &&
                    aug_comp != 0 && aug_comp != kHashNull) {
                    MultiGdbCursor ac;
                    if (MultiLookup(views, aug_comp, ac)) {
                        int an = 0;
                        for (uint32_t asl : kAugSlots) {
                            MultiGdbCursor so;
                            uint32_t ah = 0;
                            uint8_t aty = 0;
                            if (!MultiFindInherited(views, ac, asl, 0xFF,
                                                    so, ah, &aty) ||
                                ah == 0 || ah == kHashNull) {
                                continue;
                            }
                            std::string an_name = dict_name(ah);
                            if (an_name.empty()) {
                                MultiGdbCursor ar;
                                if (MultiLookup(views, ah, ar)) {
                                    MultiGdbCursor no;
                                    uint32_t nt = 0;
                                    uint8_t nty = 0;
                                    if (MultiFindInherited(
                                            views, ar, kNameTag, 0xFF,
                                            no, nt, &nty) &&
                                        (nty == 4 || nty == 7)) {
                                        an_name = dict_name(nt);
                                    }
                                }
                            }
                            if (an_name.empty()) continue;
                            char lbl[24];
                            std::snprintf(lbl, sizeof(lbl),
                                          "Augment %d", ++an);
                            d.stats.emplace_back(
                                lbl, PrettifyTagLabel(an_name));
                        }
                    }
                }
            }

            out.push_back(std::move(d));
        }
    }

    std::sort(out.begin(), out.end(),
              [](const ItemDetail& a, const ItemDetail& b) {
                  if (a.unnamed != b.unnamed) return b.unnamed;
                  const size_t n =
                      std::min(a.label.size(), b.label.size());
                  for (size_t i = 0; i < n; ++i) {
                      const int ca =
                          std::tolower((unsigned char)a.label[i]);
                      const int cb =
                          std::tolower((unsigned char)b.label[i]);
                      if (ca != cb) return ca < cb;
                  }
                  return a.label.size() < b.label.size();
              });
    return out;
}
