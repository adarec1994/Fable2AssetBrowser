std::vector<ItemCatalogEntry> BuildItemCatalog(
    const std::vector<const std::vector<uint8_t>*>& gdbs)
{
    std::vector<ItemCatalogEntry> out;

    std::vector<std::unique_ptr<GdbView>> owned;
    std::vector<const GdbView*> views;
    std::vector<bool> view_is_level;
    for (size_t gi = 0; gi < gdbs.size(); ++gi) {
        const auto* g = gdbs[gi];
        if (!g || g->empty()) continue;
        auto v = std::make_unique<GdbView>(*g);
        if (v->ok) {
            views.push_back(v.get());
            view_is_level.push_back(gi == 0);
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

    std::unordered_set<uint32_t> seen;
    for (size_t vi = 0; vi < views.size(); ++vi) {
        const GdbView& view = *views[vi];
        for (uint32_t i = 0; i < view.count; ++i) {
            if (i >= view.record_data_offsets.size()) break;
            const size_t rec = view.record_data_offsets[i];
            size_t slot = 0;
            if (!view.findLocal(rec, kHashInventoryItemComponent, 6,
                                slot, nullptr)) {
                continue;
            }
            const uint32_t rec_hash =
                ReadBeU32(view.bytes.data() + view.hash_base +
                          size_t(i) * 4);
            if (!seen.insert(rec_hash).second) continue;

            EntityContentsItem info;
            ReadContentsItemInfo(views, rec_hash, info, &dict);

            ItemCatalogEntry e;
            e.record_hash = rec_hash;
            e.money = info.money;
            e.from_level = view_is_level[vi];
            std::string raw;
            auto dit = dict.find(rec_hash);
            if (dit != dict.end() && !dit->second.empty()) {
                raw = dit->second;
            } else if (!info.name_tag.empty()) {
                raw = info.name_tag;
            }
            if (!raw.empty()) {
                e.label = PrettifyTagLabel(std::move(raw));
            } else {
                char buf[24];
                std::snprintf(buf, sizeof(buf), "unnamed 0x%08X",
                              rec_hash);
                e.label = buf;
                e.unnamed = true;
            }
            out.push_back(std::move(e));
        }
    }

    std::sort(out.begin(), out.end(),
              [](const ItemCatalogEntry& a, const ItemCatalogEntry& b) {
                  if (a.unnamed != b.unnamed) return b.unnamed;
                  const size_t n = std::min(a.label.size(),
                                            b.label.size());
                  for (size_t i = 0; i < n; ++i) {
                      const int ca = std::tolower(
                          (unsigned char)a.label[i]);
                      const int cb = std::tolower(
                          (unsigned char)b.label[i]);
                      if (ca != cb) return ca < cb;
                  }
                  return a.label.size() < b.label.size();
              });

    out.erase(std::unique(out.begin(), out.end(),
                          [](const ItemCatalogEntry& a,
                             const ItemCatalogEntry& b) {
                              return !a.unnamed && !b.unnamed &&
                                     a.money == b.money &&
                                     a.label == b.label;
                          }),
              out.end());
    return out;
}
