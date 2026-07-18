void AppendUniqueModelPathHash(std::vector<uint32_t>& out, uint32_t h)
{
    if (h == 0 || h == 0x811C9DC5u) return;
    if (std::find(out.begin(), out.end(), h) == out.end()) {
        out.push_back(h);
    }
}

template <typename Fn>
void VisitRecordAndParents(const GdbView& view, size_t record, Fn&& fn)
{
    std::unordered_set<size_t> visited;
    size_t cur = record;
    for (int depth = 0; depth < 64; ++depth) {
        if (!visited.insert(cur).second) return;
        fn(cur);

        size_t parent_slot = 0;
        if (!view.findLocal(cur, kHashParent, 6, parent_slot, nullptr)) {
            return;
        }
        const uint32_t parent_hash =
            ReadBeU32(view.bytes.data() + parent_slot);
        if (parent_hash == 0) return;

        size_t parent_record = 0;
        if (!view.lookup(parent_hash, parent_record)) return;
        cur = parent_record;
    }
}

void CollectModelFileFields(const GdbView& view,
                            size_t record,
                            std::vector<uint32_t>& out)
{
    uint32_t h = 0;
    if (TryReadModelPathHashField(view, record, kHashModelFile, h)) {
        AppendUniqueModelPathHash(out, h);
    }
    if (TryReadModelPathHashField(view, record, kHashModelFile1, h)) {
        AppendUniqueModelPathHash(out, h);
    }
    if (TryReadModelPathHashField(view, record, kHashModelFile2, h)) {
        AppendUniqueModelPathHash(out, h);
    }
}

void CollectModelFileFieldsAndParents(const GdbView& view,
                                      size_t record,
                                      std::vector<uint32_t>& out)
{
    std::unordered_set<size_t> visited;
    size_t cur = record;
    for (int depth = 0; depth < 64; ++depth) {
        if (!visited.insert(cur).second) return;

        const size_t before = out.size();
        bool has_model_file_field = false;
        size_t slot = 0;
        uint8_t field_type = 0;
        has_model_file_field =
            view.findLocal(cur, kHashModelFile, 0xFF, slot, &field_type) ||
            view.findLocal(cur, kHashModelFile1, 0xFF, slot, &field_type) ||
            view.findLocal(cur, kHashModelFile2, 0xFF, slot, &field_type);
        CollectModelFileFields(view, cur, out);
        if (has_model_file_field || out.size() != before) return;

        size_t parent_slot = 0;
        if (!view.findLocal(cur, kHashParent, 6, parent_slot, nullptr)) {
            return;
        }
        const uint32_t parent_hash =
            ReadBeU32(view.bytes.data() + parent_slot);
        if (parent_hash == 0) return;

        size_t parent_record = 0;
        if (!view.lookup(parent_hash, parent_record)) return;
        cur = parent_record;
    }
}

void CollectMeshRecordModelPathHashes(const GdbView& view,
                                      size_t mesh_record,
                                      std::vector<uint32_t>& out)
{
    CollectModelFileFieldsAndParents(view, mesh_record, out);

    size_t mesh_slot = 0;
    uint8_t mesh_type = 0;
    if (!view.findField(mesh_record, kHashStaticMultipleModelFile, 0xFF,
                        mesh_slot, &mesh_type)) {
        return;
    }
    if (mesh_slot + 4 > view.body_end) return;

    const uint32_t raw = ReadBeU32(view.bytes.data() + mesh_slot);
    if (raw == 0) return;

    if (mesh_type == 6) {
        size_t model_record = 0;
        if (view.lookup(raw, model_record)) {
            CollectModelFileFieldsAndParents(view, model_record, out);
        }

        AppendUniqueModelPathHash(out, raw);
    } else if (mesh_type == 3 || mesh_type == 4) {
        AppendUniqueModelPathHash(out, raw);
    }
}

void CollectStaticMultipleSlotModelPathHashes(const GdbView& view,
                                              size_t list_record,
                                              uint32_t slot_hash,
                                              std::vector<uint32_t>& out)
{
    size_t sch = 0;
    uint32_t n = 0;
    if (!view.schema(list_record, sch, n)) return;

    const size_t hashes = sch + 4;
    const size_t descs = hashes + size_t(n) * 4;
    for (uint32_t i = 0; i < n; ++i) {
        if (ReadBeU32(view.bytes.data() + hashes + size_t(i) * 4) !=
            slot_hash) {
            continue;
        }

        const uint32_t desc =
            ReadBeU32(view.bytes.data() + descs + size_t(i) * 4);
        if (uint8_t(desc >> 24) != 6) continue;

        const size_t item_slot = list_record + 4 + size_t(i) * 4;
        if (item_slot + 4 > view.body_end) return;

        const uint32_t item_hash = ReadBeU32(view.bytes.data() + item_slot);
        if (item_hash == 0) continue;

        size_t item_record = 0;
        if (!view.lookup(item_hash, item_record)) continue;

        CollectMeshRecordModelPathHashes(view, item_record, out);
    }
}

void CollectStaticMultipleFallbackModelPathHashes(const GdbView& view,
                                                  size_t list_record,
                                                  std::vector<uint32_t>& out)
{
    size_t sch = 0;
    uint32_t n = 0;
    if (!view.schema(list_record, sch, n)) return;

    const size_t hashes = sch + 4;
    const size_t descs = hashes + size_t(n) * 4;
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t field_hash =
            ReadBeU32(view.bytes.data() + hashes + size_t(i) * 4);
        if (field_hash == kHashParent) continue;

        const uint32_t desc =
            ReadBeU32(view.bytes.data() + descs + size_t(i) * 4);
        if (uint8_t(desc >> 24) != 6) continue;

        const size_t item_slot = list_record + 4 + size_t(i) * 4;
        if (item_slot + 4 > view.body_end) return;

        const uint32_t item_hash = ReadBeU32(view.bytes.data() + item_slot);
        if (item_hash == 0) continue;

        size_t item_record = 0;
        if (!view.lookup(item_hash, item_record)) continue;

        CollectMeshRecordModelPathHashes(view, item_record, out);
    }
}

void CollectStaticMeshModelPathHashes(const GdbView& view,
                                      size_t record,
                                      std::vector<uint32_t>& out)
{
    size_t model_slot = 0;
    if (!view.findField(record, kHashStaticMeshComponent, 6,
                        model_slot, nullptr)) {
        return;
    }

    const uint32_t model_resource_hash =
        ReadBeU32(view.bytes.data() + model_slot);
    size_t model_resource_record = 0;
    if (!view.lookup(model_resource_hash, model_resource_record)) return;

    CollectModelFileFieldsAndParents(view, model_resource_record, out);
}

void CollectAnimatedMeshModelPathHashes(const GdbView& view,
                                        size_t record,
                                        std::vector<uint32_t>& out)
{
    size_t model_slot = 0;
    if (!view.findField(record, kHashGraphicAppearanceAnimatedMeshComponent, 6,
                        model_slot, nullptr)) {
        return;
    }

    const uint32_t model_resource_hash =
        ReadBeU32(view.bytes.data() + model_slot);
    size_t model_resource_record = 0;
    if (!view.lookup(model_resource_hash, model_resource_record)) return;

    CollectModelFileFieldsAndParents(view, model_resource_record, out);
}

void CollectStaticMultipleComponentModelPathHashes(const GdbView& view,
                                                   size_t component_record,
                                                   std::vector<uint32_t>& out)
{
    size_t list_slot = 0;
    if (!view.findField(component_record, kHashStaticMultipleModelList, 6,
                        list_slot, nullptr)) {
        return;
    }

    const uint32_t list_hash = ReadBeU32(view.bytes.data() + list_slot);
    size_t list_record = 0;
    if (!view.lookup(list_hash, list_record)) return;

    CollectStaticMultipleSlotModelPathHashes(
        view, list_record, kHashStaticMultipleModelSlotA, out);
    CollectStaticMultipleSlotModelPathHashes(
        view, list_record, kHashStaticMultipleModelSlotD, out);
    CollectStaticMultipleFallbackModelPathHashes(view, list_record, out);
}

void CollectStaticMultipleModelPathHashes(const GdbView& view,
                                          size_t record,
                                          std::vector<uint32_t>& out)
{
    size_t component_slot = 0;
    if (!view.findField(record, kHashStaticMultipleMeshComponent, 6,
                        component_slot, nullptr)) {
        return;
    }

    const uint32_t component_hash =
        ReadBeU32(view.bytes.data() + component_slot);
    size_t component_record = 0;
    if (!view.lookup(component_hash, component_record)) return;

    CollectStaticMultipleComponentModelPathHashes(view, component_record, out);
}

void CollectGraphicAppearanceModelPathHashes(const GdbView& view,
                                             size_t record,
                                             std::vector<uint32_t>& out)
{
    size_t appearance_slot = 0;
    if (!view.findLocal(record, kHashGraphicAppearanceComponent, 6,
                        appearance_slot, nullptr)) {
        return;
    }

    const uint32_t appearance_hash =
        ReadBeU32(view.bytes.data() + appearance_slot);
    size_t appearance_record = 0;
    if (!view.lookup(appearance_hash, appearance_record)) return;

    VisitRecordAndParents(view, appearance_record, [&](size_t r) {
        CollectStaticMeshModelPathHashes(view, r, out);
        CollectStaticMultipleModelPathHashes(view, r, out);
        CollectStaticMultipleComponentModelPathHashes(view, r, out);
    });
}

std::vector<uint32_t> CollectModelPathHashesForRecord(const GdbView& view,
                                                      size_t record)
{
    std::vector<uint32_t> out;
    if (!view.ok) return out;
    VisitRecordAndParents(view, record, [&](size_t r) {
        CollectAnimatedMeshModelPathHashes(view, r, out);
        CollectStaticMeshModelPathHashes(view, r, out);
        CollectStaticMultipleModelPathHashes(view, r, out);
        CollectGraphicAppearanceModelPathHashes(view, r, out);
    });
    return out;
}

bool TryReadModelPathHashForRecord(const GdbView& view,
                                   size_t record,
                                   uint32_t& out_hash)
{
    out_hash = 0;
    if (!view.ok) return false;
    std::vector<uint32_t> hashes = CollectModelPathHashesForRecord(view, record);
    if (hashes.empty()) return false;
    out_hash = hashes.front();
    return true;
}

bool TryReadModelPathHashForHash(const GdbView& view,
                                 uint32_t record_hash,
                                 uint32_t& out_hash)
{
    out_hash = 0;
    if (!view.ok || record_hash == 0) return false;

    size_t record = 0;
    if (!view.lookup(record_hash, record)) return false;
    return TryReadModelPathHashForRecord(view, record, out_hash);
}
