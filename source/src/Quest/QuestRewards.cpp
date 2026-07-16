#include "QuestRewards.h"

#include "../GDB/GdbEdit.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <regex>
#include <unordered_set>

namespace Quest {
namespace {

constexpr uint32_t kParent = 0x5F6317D5u;
constexpr uint32_t kNameTag = 0x9555A6FCu;
constexpr uint32_t kInventoryItemComponent = 0xC3318103u;
constexpr uint32_t kRewardItems = 0x52E357B4u;

bool read_file(const std::string& path, std::vector<uint8_t>& bytes) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    input.seekg(0, std::ios::end);
    const std::streamoff length = input.tellg();
    if (length <= 0) return false;
    input.seekg(0, std::ios::beg);
    bytes.resize(static_cast<std::size_t>(length));
    input.read(reinterpret_cast<char*>(bytes.data()), length);
    return bool(input);
}

std::string registered_quest_record(const std::string& lua) {
    static const std::regex registration(
        R"rx(QuestTracker\.Register\s*\([^,]+,[^,]+,\s*"([^"]+)")rx",
        std::regex::icase);
    std::smatch match;
    if (std::regex_search(lua, match, registration) && match.size() >= 2) {
        return match[1].str();
    }
    static const std::regex direct_record(
        R"rx(GDB\.GetRecord\s*\(\s*"(Quest_[^"]+)")rx",
        std::regex::icase);
    if (std::regex_search(lua, match, direct_record) && match.size() >= 2) {
        return match[1].str();
    }
    return {};
}

bool inherited_field(const GdbEdit::GdbFile& gdb, uint32_t record,
                     uint32_t field_hash, GdbEdit::Field& result) {
    std::unordered_set<uint32_t> visited;
    while (record != 0 && visited.insert(record).second) {
        const int index = gdb.FindRecord(record);
        if (index < 0) return false;
        std::vector<GdbEdit::Field> fields;
        if (!gdb.Fields(index, fields)) return false;
        uint32_t parent = 0;
        for (const GdbEdit::Field& field : fields) {
            if (field.hash == field_hash) {
                result = field;
                return true;
            }
            if (field.hash == kParent) parent = field.value;
        }
        record = parent;
    }
    return false;
}

std::string dict_name(const GdbEdit::GdbFile& gdb, uint32_t hash) {
    const auto found = gdb.Dict().find(hash);
    return found == gdb.Dict().end() ? std::string() : found->second;
}

std::string reward_item_text_tag(const GdbEdit::GdbFile& gdb,
                                 uint32_t item_record) {
    GdbEdit::Field component;
    if (!inherited_field(gdb, item_record, kInventoryItemComponent,
                         component)) {
        return {};
    }
    GdbEdit::Field name;
    if (!inherited_field(gdb, component.value, kNameTag, name)) return {};
    return dict_name(gdb, name.value);
}

void append_numeric_reward(const GdbEdit::GdbFile& gdb,
                           uint32_t quest_record, uint32_t field_hash,
                           const char* label,
                           std::vector<QuestRewardReference>& rewards) {
    GdbEdit::Field field;
    if (!inherited_field(gdb, quest_record, field_hash, field)) return;
    const int32_t value = static_cast<int32_t>(field.value);
    if (value == 0) return;
    QuestRewardReference reward;
    reward.label = label;
    reward.amount = static_cast<double>(value);
    rewards.push_back(std::move(reward));
}

}

std::vector<QuestRewardReference> ExtractQuestRecordRewards(
    const std::string& globals_gdb_path,
    const std::string& decompiled_lua) {
    std::vector<QuestRewardReference> rewards;
    const std::string record_name = registered_quest_record(decompiled_lua);
    if (record_name.empty()) return rewards;

    std::vector<uint8_t> bytes;
    if (!read_file(globals_gdb_path, bytes)) return rewards;
    GdbEdit::GdbFile gdb;
    std::string error;
    if (!gdb.Parse(bytes, error)) return rewards;
    const uint32_t quest_record = gdb.ResolveNamedRecord(record_name);
    if (quest_record == 0) return rewards;

    append_numeric_reward(gdb, quest_record, 0xD0409A44u, "Gold", rewards);
    append_numeric_reward(gdb, quest_record, 0x57C696DDu, "Renown", rewards);
    append_numeric_reward(gdb, quest_record, 0xB5D8128Cu,
                          "General experience", rewards);
    append_numeric_reward(gdb, quest_record, 0x3CC2F547u,
                          "Strength experience", rewards);
    append_numeric_reward(gdb, quest_record, 0x395D0973u,
                          "Skill experience", rewards);
    append_numeric_reward(gdb, quest_record, 0xA28AA618u,
                          "Will experience", rewards);

    GdbEdit::Field reward_items;
    if (!inherited_field(gdb, quest_record, kRewardItems, reward_items)) {
        return rewards;
    }
    const int container_index = gdb.FindRecord(reward_items.value);
    if (container_index < 0) return rewards;
    std::vector<GdbEdit::Field> fields;
    if (!gdb.Fields(container_index, fields)) return rewards;
    for (const GdbEdit::Field& field : fields) {
        if (field.hash == kParent || (field.type != 6 && field.type != 7) ||
            field.value == 0) {
            continue;
        }
        QuestRewardReference reward;
        reward.label = dict_name(gdb, field.hash);
        if (reward.label.empty()) reward.label = "Item";
        reward.item_text_tag = reward_item_text_tag(gdb, field.value);
        rewards.push_back(std::move(reward));
    }
    return rewards;
}

}
