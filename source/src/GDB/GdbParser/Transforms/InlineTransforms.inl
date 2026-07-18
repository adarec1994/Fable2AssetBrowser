bool TryReadInlineVec3(const std::vector<uint8_t>& bytes,
                       size_t q,
                       size_t re,
                       float& x,
                       float& y,
                       float& z)
{
    if (q + 16 > re || q + 16 > bytes.size()) return false;
    if (ReadBeU32(bytes.data() + q) != kInlineVec3SchemaRel) return false;
    const float rz = ReadBeF32(bytes.data() + q + 4);
    const float ry = ReadBeF32(bytes.data() + q + 8);
    const float rx = ReadBeF32(bytes.data() + q + 12);
    if (!Finite3(rx, ry, rz)) return false;
    x = rx;
    y = ry;
    z = rz;
    return true;
}

bool FindNearbyInlineRotation(const std::vector<uint8_t>& bytes,
                              size_t rs,
                              size_t re,
                              size_t pos_offset,
                              float& rot_x,
                              float& rot_y,
                              float& rot_z)
{
    const size_t back_begin =
        (pos_offset > rs + 0x60) ? pos_offset - 0x60 : rs;
    for (size_t q = pos_offset; q >= back_begin + 4;) {
        q -= 4;
        float cx = 0.0f, cy = 0.0f, cz = 0.0f;
        if (TryReadInlineVec3(bytes, q, re, cx, cy, cz) &&
            LooksLikeRotationTriplet(cx, cy, cz))
        {
            rot_x = cx;
            rot_y = cy;
            rot_z = cz;
            return true;
        }
        if (q == 0) break;
    }

    const size_t fwd_end = std::min(re, pos_offset + 0x60);
    for (size_t q = pos_offset + 4; q + 16 <= fwd_end; q += 4) {
        float cx = 0.0f, cy = 0.0f, cz = 0.0f;
        if (TryReadInlineVec3(bytes, q, re, cx, cy, cz) &&
            LooksLikeRotationTriplet(cx, cy, cz))
        {
            rot_x = cx;
            rot_y = cy;
            rot_z = cz;
            return true;
        }
    }
    return false;
}

std::vector<InlineTransformCandidate> CollectInlineTransforms(
    const std::vector<uint8_t>& bytes,
    size_t rs,
    size_t re)
{
    std::vector<InlineTransformCandidate> out;
    for (size_t q = rs + 4; q + 16 <= re; q += 4) {
        InlineTransformCandidate c;
        if (!TryReadInlineVec3(bytes, q, re, c.x, c.y, c.z)) continue;
        if (!PlausiblePosition(c.x, c.y, c.z)) continue;
        c.pos_slots[0] = q + 12;
        c.pos_slots[1] = q + 8;
        c.pos_slots[2] = q + 4;
        if (LooksLikeRotationTriplet(c.x, c.y, c.z)) continue;
        c.has_rotation = FindNearbyInlineRotation(
            bytes, rs, re, q, c.rot_x, c.rot_y, c.rot_z);
        bool duplicate = false;
        for (const InlineTransformCandidate& existing : out) {
            const float dx = existing.x - c.x;
            const float dy = existing.y - c.y;
            const float dz = existing.z - c.z;
            if ((dx * dx + dy * dy + dz * dz) < 0.01f) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) out.push_back(c);
    }
    return out;
}

bool TryInlineTransform(const std::vector<uint8_t>& bytes,
                        size_t rs,
                        size_t re,
                        float& x,
                        float& y,
                        float& z,
                        float& rot_x,
                        float& rot_y,
                        float& rot_z,
                        bool& has_rotation,
                        size_t* out_pos_slots = nullptr) {
    std::vector<InlineTransformCandidate> candidates =
        CollectInlineTransforms(bytes, rs, re);
    if (candidates.empty()) return false;
    const InlineTransformCandidate& c = candidates.front();
    x = c.x;
    y = c.y;
    z = c.z;
    rot_x = c.rot_x;
    rot_y = c.rot_y;
    rot_z = c.rot_z;
    has_rotation = c.has_rotation;
    if (out_pos_slots) {
        out_pos_slots[0] = c.pos_slots[0];
        out_pos_slots[1] = c.pos_slots[1];
        out_pos_slots[2] = c.pos_slots[2];
    }
    return true;
}

bool SameInlineTransformPosition(const InlineTransformCandidate& c,
                                 const Placement& p)
{
    const float dx = c.x - p.x;
    const float dy = c.y - p.y;
    const float dz = c.z - p.z;
    return (dx * dx + dy * dy + dz * dz) < 0.01f;
}

bool TryInlineTransformRange(const std::vector<uint8_t>& bytes,
                             size_t begin,
                             size_t end,
                             float& x,
                             float& y,
                             float& z,
                             float& rot_x,
                             float& rot_y,
                             float& rot_z,
                             bool& has_rotation,
                             size_t* out_pos_slots = nullptr) {
    if (begin >= end || end > bytes.size()) return false;
    const size_t shim = (begin >= 4) ? begin - 4 : begin;
    return TryInlineTransform(bytes, shim, end, x, y, z,
                              rot_x, rot_y, rot_z, has_rotation,
                              out_pos_slots);
}

bool TryIndexedInlineTransform(const GdbView& view,
                               size_t record,
                               float& x,
                               float& y,
                               float& z,
                               float& rot_x,
                               float& rot_y,
                               float& rot_z,
                               bool& has_rotation,
                               size_t* out_pos_slots = nullptr) {
    size_t payload_start = 0;
    size_t payload_end = 0;
    if (!view.payloadRange(record, payload_start, payload_end)) return false;
    return TryInlineTransformRange(view.bytes, payload_start, payload_end,
                                   x, y, z, rot_x, rot_y, rot_z,
                                   has_rotation, out_pos_slots);
}

bool TryEmbeddedTransformRecords(const GdbView& view,
                                 size_t rs,
                                 size_t re,
                                 float& x,
                                 float& y,
                                 float& z,
                                 float& rot_x,
                                 float& rot_y,
                                 float& rot_z,
                                 bool& has_rotation) {
    for (size_t q = rs + 4; q + 12 <= re && q + 4 <= view.body_end; q += 4) {
        size_t sch = 0;
        uint32_t n = 0;
        if (!view.schema(q, sch, n)) continue;
        size_t slot = 0;
        if (!view.findLocal(q, kHashPosition, 6, slot, nullptr)) continue;
        if (TryTransformRecord(view, q, x, y, z,
                               rot_x, rot_y, rot_z, has_rotation)) {
            return true;
        }
    }
    return false;
}

bool TryPrefixedPlacementRecord(const GdbView& view,
                                size_t rs,
                                size_t re,
                                size_t& record,
                                float& x,
                                float& y,
                                float& z,
                                float& rot_x,
                                float& rot_y,
                                float& rot_z,
                                bool& has_rotation) {

    if (!view.ok || rs + 16 > re) return false;
    record = rs + 12;

    size_t schema_off = 0;
    uint32_t field_count = 0;
    if (!view.schema(record, schema_off, field_count)) return false;
    if (view.hasVectorSchema(record)) return false;

    if (TryComponentTransformRecord(view, record, x, y, z,
                                    rot_x, rot_y, rot_z, has_rotation)) {
        return true;
    }
    return TryTransformRecord(view, record, x, y, z,
                              rot_x, rot_y, rot_z, has_rotation);
}
