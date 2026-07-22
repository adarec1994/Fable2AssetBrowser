bool TryTransformRecord(const GdbView& view,
                        size_t record,
                        float& x,
                        float& y,
                        float& z,
                        float& rot_x,
                        float& rot_y,
                        float& rot_z,
                        bool& has_rotation,
                        size_t* out_pos_slots = nullptr,
                        size_t* out_rot_slots = nullptr,
                        bool lenient = false) {
    size_t pos_slot = 0;
    size_t pos_owner = 0;
    if (!view.findFieldOwner(record, kHashPosition, 6,
                             pos_slot, pos_owner, nullptr)) {
        return false;
    }
    const uint32_t pos_hash = ReadBeU32(view.bytes.data() + pos_slot);
    if (!view.readVec3Ref(pos_hash, x, y, z, nullptr, nullptr, nullptr,
                          out_pos_slots)) return false;
    if (lenient) {
        if (!Finite3(x, y, z)) return false;
    } else if (!PlausiblePosition(x, y, z)) {
        return false;
    }

    size_t rot_slot = 0;
    float rx = 0.0f, ry = 0.0f, rz = 0.0f;
    if (view.findLocal(pos_owner, kHashRotation, 6, rot_slot, nullptr)) {
        const uint32_t rot_hash = ReadBeU32(view.bytes.data() + rot_slot);
        size_t rslots[3] = {0, 0, 0};
        if (view.readRotationVec3Ref(rot_hash, rx, ry, rz, rslots)) {
            if (Finite3(rx, ry, rz)) {
                rot_x = rx;
                rot_y = ry;
                rot_z = rz;
                has_rotation = true;
                if (out_rot_slots) {
                    out_rot_slots[0] = rslots[0];
                    out_rot_slots[1] = rslots[1];
                    out_rot_slots[2] = rslots[2];
                }
            }
        }
    }
    return true;
}

bool TryComponentTransformField(const GdbView& view,
                                size_t record,
                                uint32_t component_field_hash,
                                float& x,
                                float& y,
                                float& z,
                                float& rot_x,
                                float& rot_y,
                                float& rot_z,
                                bool& has_rotation,
                                size_t* out_pos_slots = nullptr,
                                size_t* out_rot_slots = nullptr,
                                bool lenient = false) {
    size_t transform_slot = 0;
    if (!view.findLocal(record, component_field_hash, 6,
                        transform_slot, nullptr)) {
        return false;
    }

    const uint32_t transform_hash =
        ReadBeU32(view.bytes.data() + transform_slot);
    size_t transform_record = 0;
    if (!view.lookup(transform_hash, transform_record)) return false;
    return TryTransformRecord(view, transform_record, x, y, z,
                              rot_x, rot_y, rot_z, has_rotation,
                              out_pos_slots, out_rot_slots, lenient);
}

bool TryComponentTransformRecord(const GdbView& view,
                                 size_t record,
                                 float& x,
                                 float& y,
                                 float& z,
                                 float& rot_x,
                                 float& rot_y,
                                 float& rot_z,
                                 bool& has_rotation,
                                 size_t* out_pos_slots = nullptr,
                                 size_t* out_rot_slots = nullptr,
                                 bool lenient = false) {
    if (TryComponentTransformField(view, record, kHashTransformComponent,
                                   x, y, z, rot_x, rot_y, rot_z,
                                   has_rotation, out_pos_slots,
                                   out_rot_slots, lenient)) {
        return true;
    }
    if (TryComponentTransformField(view, record,
                                   kHashSimpleTransformComponent,
                                   x, y, z, rot_x, rot_y, rot_z,
                                   has_rotation, out_pos_slots,
                                   out_rot_slots, lenient)) {
        return true;
    }
    if (TryComponentTransformField(
            view, record, kHashPhysicsSimulationKeyframedComponent,
            x, y, z, rot_x, rot_y, rot_z, has_rotation, out_pos_slots,
            out_rot_slots, lenient)) {
        return true;
    }
    if (TryComponentTransformField(
            view, record, kHashPhysicsSimulationStaticComponent,
            x, y, z, rot_x, rot_y, rot_z, has_rotation, out_pos_slots,
            out_rot_slots, lenient)) {
        return true;
    }
    return TryComponentTransformField(
        view, record, kHashPhysicsSimulationDynamicComponent,
        x, y, z, rot_x, rot_y, rot_z, has_rotation, out_pos_slots,
        out_rot_slots, lenient);

}
