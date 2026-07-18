bool TryReadModelPathHashField(const GdbView& view,
                               size_t record,
                               uint32_t field_hash,
                               uint32_t& out_hash)
{
    out_hash = 0;
    size_t path_hash_slot = 0;
    uint8_t path_hash_type = 0;
    if (!view.findLocal(record, field_hash, 0xFF,
                        path_hash_slot, &path_hash_type)) {
        return false;
    }
    if (path_hash_type != 4 && path_hash_type != 3) return false;

    out_hash = ReadBeU32(view.bytes.data() + path_hash_slot);
    return out_hash != 0 && out_hash != 0x811C9DC5u;
}

bool TryReadInheritedHashField(const GdbView& view,
                               size_t record,
                               uint32_t field_hash,
                               uint32_t& out_hash)
{
    out_hash = 0;
    size_t slot = 0;
    uint8_t type = 0;
    if (!view.findField(record, field_hash, 0xFF, slot, &type)) {
        return false;
    }

    if (type != 3 && type != 4 && type != 6) return false;
    out_hash = ReadBeU32(view.bytes.data() + slot);
    return out_hash != 0 && out_hash != 0x811C9DC5u;
}

bool TryReadStaticMeshModelPathHash(const GdbView& view,
                                    size_t record,
                                    uint32_t& out_hash)
{
    out_hash = 0;
    size_t model_slot = 0;
    if (!view.findField(record, kHashStaticMeshComponent, 6,
                        model_slot, nullptr)) {
        return false;
    }

    const uint32_t model_resource_hash =
        ReadBeU32(view.bytes.data() + model_slot);
    size_t model_resource_record = 0;
    if (!view.lookup(model_resource_hash, model_resource_record)) return false;

    return TryReadModelPathHashField(view, model_resource_record,
                                     kHashModelFile, out_hash);
}

bool TryReadStaticMultipleSlotModelPathHash(const GdbView& view,
                                            size_t list_record,
                                            uint32_t slot_hash,
                                            uint32_t& out_hash)
{
    out_hash = 0;
    size_t sch = 0;
    uint32_t n = 0;
    if (!view.schema(list_record, sch, n)) return false;

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
        if (item_slot + 4 > view.body_end) return false;

        const uint32_t item_hash = ReadBeU32(view.bytes.data() + item_slot);
        if (item_hash == 0) continue;

        size_t item_record = 0;
        if (!view.lookup(item_hash, item_record)) continue;

        if (TryReadModelPathHashField(view, item_record,
                                      kHashStaticMultipleModelFile,
                                      out_hash)) {
            return true;
        }
    }

    return false;
}

bool TryReadStaticMultipleFallbackModelPathHash(const GdbView& view,
                                                size_t list_record,
                                                uint32_t& out_hash)
{
    out_hash = 0;
    size_t sch = 0;
    uint32_t n = 0;
    if (!view.schema(list_record, sch, n)) return false;

    const size_t hashes = sch + 4;
    const size_t descs = hashes + size_t(n) * 4;
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t field_hash =
            ReadBeU32(view.bytes.data() + hashes + size_t(i) * 4);
        if (field_hash == kHashParent ||
            field_hash == kHashStaticMultipleModelSlotA ||
            field_hash == kHashStaticMultipleModelSlotD) {
            continue;
        }

        const uint32_t desc =
            ReadBeU32(view.bytes.data() + descs + size_t(i) * 4);
        if (uint8_t(desc >> 24) != 6) continue;

        const size_t item_slot = list_record + 4 + size_t(i) * 4;
        if (item_slot + 4 > view.body_end) return false;

        const uint32_t item_hash = ReadBeU32(view.bytes.data() + item_slot);
        if (item_hash == 0) continue;

        size_t item_record = 0;
        if (!view.lookup(item_hash, item_record)) continue;

        if (TryReadModelPathHashField(view, item_record,
                                      kHashStaticMultipleModelFile,
                                      out_hash)) {
            return true;
        }
    }

    return false;
}

bool TryReadStaticMultipleModelPathHash(const GdbView& view,
                                        size_t record,
                                        uint32_t& out_hash)
{
    out_hash = 0;
    size_t component_slot = 0;
    if (!view.findField(record, kHashStaticMultipleMeshComponent, 6,
                        component_slot, nullptr)) {
        return false;
    }

    const uint32_t component_hash =
        ReadBeU32(view.bytes.data() + component_slot);
    size_t component_record = 0;
    if (!view.lookup(component_hash, component_record)) return false;

    size_t list_slot = 0;
    if (!view.findField(component_record, kHashStaticMultipleModelList, 6,
                        list_slot, nullptr)) {
        return false;
    }

    const uint32_t list_hash = ReadBeU32(view.bytes.data() + list_slot);
    size_t list_record = 0;
    if (!view.lookup(list_hash, list_record)) return false;

    if (TryReadStaticMultipleSlotModelPathHash(view, list_record,
                                               kHashStaticMultipleModelSlotA,
                                               out_hash)) {
        return true;
    }
    if (TryReadStaticMultipleSlotModelPathHash(view, list_record,
                                               kHashStaticMultipleModelSlotD,
                                               out_hash)) {
        return true;
    }
    return TryReadStaticMultipleFallbackModelPathHash(view, list_record,
                                                     out_hash);
}
