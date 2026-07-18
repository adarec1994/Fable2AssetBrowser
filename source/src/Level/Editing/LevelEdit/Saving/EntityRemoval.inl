bool remove_spawn_point_reference(
    GdbEdit::GdbFile& g,
    const Gdb::SpawnDonorInfo& donor,
    const ModuleState::SpawnPointDelete& deletion,
    std::string& err)
{
    constexpr uint32_t kSpawnPoints = 0x559B5DBFu;
    uint32_t list_hash = deletion.spawn_points_record;




    GdbEdit::Field component;
    GdbEdit::Field points;
    if (deletion.generator_entity && donor.gen_comp_field &&
        g.FindLocalField(deletion.generator_entity,
                         donor.gen_comp_field, component) &&
        component.type == 6 &&
        g.FindLocalField(component.value, kSpawnPoints, points) &&
        points.type == 6) {
        list_hash = points.value;
    }

    std::vector<GdbEdit::Field> fields;
    if (!g.Fields(g.FindRecord(list_hash), fields)) {
        err = "spawn point list is unreadable";
        return false;
    }
    for (const auto& field : fields) {
        if (field.type == 7 &&
            field.value == deletion.spawn_point_entity) {
            if (!g.RemoveField(list_hash, field.hash)) {
                err = "spawn point list field removal failed";
                return false;
            }
            return true;
        }
    }
    err = "spawn point is no longer present in its generator";
    return false;
}

size_t remove_save_entities(
    std::vector<uint8_t>& xml_bytes,
    const std::unordered_set<uint32_t>& entity_hashes)
{
    if (entity_hashes.empty()) return 0;
    std::string xml(reinterpret_cast<const char*>(xml_bytes.data()),
                    xml_bytes.size());
    constexpr const char* kClose = "</Entity>";
    constexpr size_t kCloseLen = 9;
    size_t removed = 0;
    size_t pos = 0;
    while ((pos = xml.find("<Entity ", pos)) != std::string::npos) {
        const size_t value_begin = xml.find('>', pos);
        const size_t close = value_begin == std::string::npos
                                 ? std::string::npos
                                 : xml.find(kClose, value_begin + 1);
        if (value_begin == std::string::npos || close == std::string::npos) {
            break;
        }
        const std::string value =
            xml.substr(value_begin + 1, close - value_begin - 1);
        char* parse_end = nullptr;
        const unsigned long parsed =
            std::strtoul(value.c_str(), &parse_end, 0);
        if (parse_end != value.c_str() &&
            entity_hashes.count(uint32_t(parsed))) {
            size_t erase_begin = xml.rfind('\n', pos);
            erase_begin = erase_begin == std::string::npos
                              ? 0
                              : erase_begin + 1;
            for (size_t i = erase_begin; i < pos; ++i) {
                if (xml[i] != ' ' && xml[i] != '\t' && xml[i] != '\r') {
                    erase_begin = pos;
                    break;
                }
            }
            size_t erase_end = close + kCloseLen;
            if (erase_end < xml.size() && xml[erase_end] == '\r') {
                ++erase_end;
            }
            if (erase_end < xml.size() && xml[erase_end] == '\n') {
                ++erase_end;
            }
            xml.erase(erase_begin, erase_end - erase_begin);
            pos = erase_begin;
            ++removed;
        } else {
            pos = close + kCloseLen;
        }
    }
    xml_bytes.assign(xml.begin(), xml.end());
    return removed;
}
