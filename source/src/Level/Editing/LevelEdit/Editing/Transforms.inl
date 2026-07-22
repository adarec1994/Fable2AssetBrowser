bool SetEnabled(bool on, std::string& msg) {
    if (on && !GameBackup::RequireBackup(msg)) return false;
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (!on) {
        s.enabled = false;
        msg = "level edit mode off";
        return true;
    }
    if (!s.available) {
        msg = "no level loaded";
        return false;
    }
    if (s.enabled) {
        msg = "level edit mode on";
        return true;
    }
    if (s.saving) {
        msg = "busy";
        return false;
    }
    s.enabled = true;
    msg = "level edit mode on";
    return true;
}

bool EditFor(uint32_t selection_id,
             float out_pos_delta[3],
             float out_rot_delta_deg[3]) {
    std::lock_guard<std::mutex> lk(mtx());
    auto it = st().edits.find(selection_id);
    if (it == st().edits.end() || !it->second.changed()) return false;
    const EditEntry& e = it->second;
    if (out_pos_delta) {
        out_pos_delta[0] = e.delta[0];
        out_pos_delta[1] = e.delta[1];
        out_pos_delta[2] = e.delta[2];
    }
    if (out_rot_delta_deg) {
        out_rot_delta_deg[0] = e.rot_deg[0];
        out_rot_delta_deg[1] = e.rot_deg[1];
        out_rot_delta_deg[2] = e.rot_deg[2];
    }
    return true;
}

void AddMove(uint32_t selection_id, const float step[3],
             const InstInfo& info) {
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (s.saving) return;
    auto& e = s.edits[selection_id];
    register_entry(e, info);
    e.delta[0] += step[0];
    e.delta[1] += step[1];
    e.delta[2] += step[2];
    s.dirty = true;
    ++s.revision;
}

void AddRotate(uint32_t selection_id, const float step_deg[3],
               const InstInfo& info) {
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (s.saving) return;
    auto& e = s.edits[selection_id];
    register_entry(e, info);
    for (int i = 0; i < 3; ++i) {
        e.rot_deg[i] += step_deg[i];
        while (e.rot_deg[i] > 180.0f)  e.rot_deg[i] -= 360.0f;
        while (e.rot_deg[i] < -180.0f) e.rot_deg[i] += 360.0f;
    }
    s.dirty = true;
    ++s.revision;
}

void SetDeleted(uint32_t selection_id, const InstInfo& info) {
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (s.saving) return;
    auto& e = s.edits[selection_id];
    register_entry(e, info);
    e.deleted = true;
    s.dirty = true;
    ++s.revision;
}

void ClearDeleted(uint32_t selection_id) {
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (s.saving) return;
    auto it = s.edits.find(selection_id);
    if (it == s.edits.end() || !it->second.deleted) return;
    it->second.deleted = false;
    s.dirty = true;
    ++s.revision;
}

bool IsDeleted(uint32_t selection_id) {
    std::lock_guard<std::mutex> lk(mtx());
    const auto& s = st();
    auto it = s.edits.find(selection_id);
    return it != s.edits.end() && it->second.deleted;
}

bool EntityRemovalPending(uint32_t entity_hash) {
    if (!entity_hash) return false;
    std::lock_guard<std::mutex> lk(mtx());
    for (const auto& kv : st().edits) {
        const EditEntry& e = kv.second;
        if (e.deleted && e.gdb_entity_hash == entity_hash) return true;
    }
    return false;
}

void NoteExternalEdit() {
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (!s.available || s.saving) return;
    s.dirty = true;
    ++s.revision;
}

int AddPlacement(const std::string& model_path, const float pos[3]) {
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (!s.available || s.saving || model_path.empty()) return -1;
    Addition a;
    a.model_path = model_path;
    a.pos[0] = pos[0];
    a.pos[1] = pos[1];
    a.pos[2] = pos[2];
    s.additions.push_back(std::move(a));
    s.dirty = true;
    ++s.revision;
    return (int)s.additions.size() - 1;
}

void GetAdditions(std::vector<Addition>& out) {
    std::lock_guard<std::mutex> lk(mtx());
    out = st().additions;
}

void MoveAddition(int index, const float pos[3]) {
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (index < 0 || index >= (int)s.additions.size()) return;
    Addition& a = s.additions[(size_t)index];
    a.pos[0] = pos[0];
    a.pos[1] = pos[1];
    a.pos[2] = pos[2];
    s.dirty = true;
    ++s.revision;
}

void SetAdditionYaw(int index, float yaw_deg) {
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (index < 0 || index >= (int)s.additions.size()) return;
    s.additions[(size_t)index].yaw_deg = yaw_deg;
    s.dirty = true;
    ++s.revision;
}

void RemoveAddition(int index) {
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (index < 0 || index >= (int)s.additions.size()) return;
    s.additions[(size_t)index].removed = true;
    s.dirty = true;
    ++s.revision;
}

void CollectPreviewXforms(
    std::unordered_map<uint32_t, EditXform>& out) {
    std::lock_guard<std::mutex> lk(mtx());
    out.clear();
    for (const auto& kv : st().edits) {
        const EditEntry& e = kv.second;
        if (!e.changed()) continue;
        EditXform x;
        x.off[0] = e.delta[0];
        x.off[1] = e.delta[2];
        x.off[2] = e.delta[1];
        x.pivot[0] = e.orig[0];
        x.pivot[1] = e.orig[2];
        x.pivot[2] = e.orig[1];
        if (e.rotated()) {
            euler_engine_to_preview_quat(e.rot_deg, x.quat);
        }
        x.has_rs = e.rotated();
        x.deleted = e.deleted;
        out[kv.first] = x;
    }
}

void PushUndoSnapshot(const std::vector<uint32_t>& ids) {
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (s.saving) return;
    UndoStep step;
    step.before.reserve(ids.size());
    for (uint32_t id : ids) {
        auto it = s.edits.find(id);
        UndoState u{{0, 0, 0}, {0, 0, 0}, false};
        if (it != s.edits.end()) {
            const EditEntry& e = it->second;
            u.delta[0] = e.delta[0];
            u.delta[1] = e.delta[1];
            u.delta[2] = e.delta[2];
            u.rot_deg[0] = e.rot_deg[0];
            u.rot_deg[1] = e.rot_deg[1];
            u.rot_deg[2] = e.rot_deg[2];
            u.deleted = e.deleted;
        }
        step.before.emplace_back(id, u);
    }
    s.undo_stack.push_back(std::move(step));
    if (s.undo_stack.size() > kMaxUndoSteps) {
        s.undo_stack.erase(s.undo_stack.begin());
    }
}

bool Undo() {
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (s.saving) return false;
    if (s.undo_stack.empty()) return false;
    UndoStep step = std::move(s.undo_stack.back());
    s.undo_stack.pop_back();
    for (const auto& kv : step.before) {
        auto it = s.edits.find(kv.first);
        if (it == s.edits.end()) continue;
        EditEntry& e = it->second;
        const UndoState& u = kv.second;
        e.delta[0] = u.delta[0];
        e.delta[1] = u.delta[1];
        e.delta[2] = u.delta[2];
        e.rot_deg[0] = u.rot_deg[0];
        e.rot_deg[1] = u.rot_deg[1];
        e.rot_deg[2] = u.rot_deg[2];
        e.deleted = u.deleted;
    }
    s.dirty = true;
    ++s.revision;
    return true;
}
