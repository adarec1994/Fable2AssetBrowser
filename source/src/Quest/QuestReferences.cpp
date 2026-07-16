#include "QuestReferences.h"

#include "GDB/GdbReaderInternal.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <iomanip>
#include <iterator>
#include <optional>
#include <regex>
#include <sstream>
#include <unordered_set>

namespace Quest {
namespace {

using Gdb::detail::GdbView;
using Gdb::detail::ReadBeF32;
using Gdb::detail::ReadBeU32;

uint32_t fnv1(const std::string& value, bool lower = false) {
    uint32_t hash = 0x811C9DC5u;
    for (unsigned char c : value) {
        hash *= 0x01000193u;
        if (lower) c = static_cast<unsigned char>(std::tolower(c));
        hash ^= c;
    }
    return hash;
}

std::string lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    return value;
}

void append_unique(std::vector<std::string>& values, const std::string& value) {
    if (value.empty()) return;
    const std::string key = lower_ascii(value);
    for (const std::string& existing : values) {
        if (lower_ascii(existing) == key) return;
    }
    values.push_back(value);
}

std::unordered_map<uint32_t, std::string> embedded_dictionary(
    const std::vector<uint8_t>& bytes, const GdbView& view) {
    std::unordered_map<uint32_t, std::string> out;
    if (!view.ok || bytes.size() < 0x18) return out;
    const uint32_t name_pairs = ReadBeU32(bytes.data() + 0x10);
    const size_t meta_end = view.offset_base + size_t(view.count) * 2;
    const size_t name_base = (meta_end + 3) & ~size_t(3);
    const size_t dict_base = name_base + size_t(name_pairs) * 8;
    if (dict_base + 12 > bytes.size() ||
        ReadBeU32(bytes.data() + dict_base) != 0x00010000u) return out;
    const uint32_t data_bytes = ReadBeU32(bytes.data() + dict_base + 4);
    const uint32_t string_count = ReadBeU32(bytes.data() + dict_base + 8);
    const size_t data_start = dict_base + 12;
    if (data_start + data_bytes > bytes.size()) return out;

    size_t offset = data_start;
    const size_t data_end = data_start + data_bytes;
    out.reserve(string_count);
    for (uint32_t i = 0; i < string_count && offset + 5 <= data_end; ++i) {
        const uint32_t hash = ReadBeU32(bytes.data() + offset);
        offset += 4;
        size_t terminator = offset;
        while (terminator < data_end && bytes[terminator] != 0) ++terminator;
        out.emplace(hash, std::string(bytes.begin() + offset,
                                      bytes.begin() + terminator));
        offset = terminator + 1;
    }
    return out;
}

std::unordered_map<uint32_t, uint32_t> named_records(
    const std::vector<uint8_t>& bytes, const GdbView& view) {
    std::unordered_map<uint32_t, uint32_t> out;
    if (!view.ok || bytes.size() < 0x18) return out;
    const uint32_t name_pairs = ReadBeU32(bytes.data() + 0x10);
    const size_t meta_end = view.offset_base + size_t(view.count) * 2;
    const size_t name_base = (meta_end + 3) & ~size_t(3);
    if (name_base + size_t(name_pairs) * 8 > bytes.size()) return out;
    out.reserve(name_pairs);
    for (uint32_t i = 0; i < name_pairs; ++i) {
        const size_t offset = name_base + size_t(i) * 8;
        out.emplace(ReadBeU32(bytes.data() + offset),
                    ReadBeU32(bytes.data() + offset + 4));
    }
    return out;
}

bool looks_like_dialogue_tag(const std::string& value) {
    return lower_ascii(value).rfind("text_", 0) == 0;
}

bool looks_like_speaker_field(const std::string& value) {
    const std::string lower = lower_ascii(value);
    return lower.find("speaker") != std::string::npos ||
           lower.find("character") != std::string::npos ||
           lower.find("creature") != std::string::npos ||
           lower.find("entity") != std::string::npos ||
           lower.find("actor") != std::string::npos;
}

bool looks_like_speaker_value(const std::string& value,
                              const std::string& cutscene_id) {
    const std::string lower = lower_ascii(value);
    if (lower.empty() || lower == lower_ascii(cutscene_id) ||
        looks_like_dialogue_tag(value)) return false;
    if (lower.find("component") != std::string::npos ||
        lower.find("cutscene") != std::string::npos ||
        lower.find("animation") != std::string::npos) return false;
    return true;
}

struct RecordField {
    std::string name;
    uint8_t type = 0;
    uint32_t order = 0;
    uint32_t value = 0;
    size_t slot = 0;
};

std::vector<RecordField> record_fields(
    const std::vector<uint8_t>& bytes, const GdbView& view, size_t record,
    const std::unordered_map<uint32_t, std::string>& dictionary) {
    std::vector<RecordField> result;
    size_t schema = 0;
    uint32_t count = 0;
    if (!view.schema(record, schema, count)) return result;
    const size_t hashes = schema + 4;
    const size_t descriptors = hashes + size_t(count) * 4;
    result.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t field_hash =
            ReadBeU32(bytes.data() + hashes + size_t(i) * 4);
        const uint32_t descriptor =
            ReadBeU32(bytes.data() + descriptors + size_t(i) * 4);
        const size_t slot = record + 4 + size_t(i) * 4;
        if (slot + 4 > view.body_end) break;
        RecordField field;
        const auto name = dictionary.find(field_hash);
        field.name = name == dictionary.end() ? std::string() : name->second;
        field.type = uint8_t(descriptor >> 24);
        field.order = descriptor & 0x00FFFFFFu;
        field.value = ReadBeU32(bytes.data() + slot);
        field.slot = slot;
        result.push_back(std::move(field));
    }
    return result;
}

const RecordField* find_field(const std::vector<RecordField>& fields,
                              const std::string& name) {
    const std::string key = lower_ascii(name);
    for (const RecordField& field : fields) {
        if (lower_ascii(field.name) == key) return &field;
    }
    return nullptr;
}

std::string field_string(
    const std::vector<RecordField>& fields, const std::string& name,
    const std::unordered_map<uint32_t, std::string>& dictionary) {
    const RecordField* field = find_field(fields, name);
    if (!field) return {};
    const auto value = dictionary.find(field->value);
    return value == dictionary.end() ? std::string() : value->second;
}

std::optional<float> field_float(const std::vector<uint8_t>& bytes,
                                 const std::vector<RecordField>& fields,
                                 const std::string& name) {
    const RecordField* field = find_field(fields, name);
    if (!field || field->type != 3 || field->slot + 4 > bytes.size()) {
        return std::nullopt;
    }
    return ReadBeF32(bytes.data() + field->slot);
}

bool field_bool(const std::vector<RecordField>& fields,
                const std::string& name) {
    const RecordField* field = find_field(fields, name);
    return field && field->value != 0;
}

std::string readable_identifier(std::string value) {
    const std::string lower = lower_ascii(value);
    if (lower == "playerhero") return "Sparrow";
    if (lower == "creaturedoghero") return "the dog";


    if (!value.empty() && (value[0] == 'Q' || value[0] == 'q')) {
        size_t i = 1;
        while (i < value.size() &&
               std::isalpha(static_cast<unsigned char>(value[i]))) ++i;
        const size_t digits = i;
        while (i < value.size() &&
               std::isdigit(static_cast<unsigned char>(value[i]))) ++i;
        if (i > digits && i < value.size() && value[i] == '_') {
            value.erase(0, i + 1);
        }
    }

    std::string result;
    result.reserve(value.size() + 8);
    for (size_t i = 0; i < value.size(); ++i) {
        const unsigned char current = static_cast<unsigned char>(value[i]);
        const unsigned char previous = i == 0
            ? 0 : static_cast<unsigned char>(value[i - 1]);
        if (current == '_' || current == '-') {
            if (!result.empty() && result.back() != ' ') result.push_back(' ');
            continue;
        }
        if (i > 0 && std::isupper(current) && std::islower(previous) &&
            !result.empty() && result.back() != ' ') {
            result.push_back(' ');
        }
        result.push_back(char(current));
    }
    return result.empty() ? value : result;
}

std::string format_number(float value) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(2) << value;
    std::string result = stream.str();
    while (result.size() > 1 && result.back() == '0') result.pop_back();
    if (!result.empty() && result.back() == '.') result.pop_back();
    return result;
}

std::optional<std::array<float, 3>> position_from_record(
    const std::vector<uint8_t>& bytes, const GdbView& view, size_t record,
    const std::unordered_map<uint32_t, std::string>& dictionary,
    std::unordered_set<size_t>& visited, int depth) {
    if (depth > 8 || !visited.insert(record).second) return std::nullopt;

    float x = 0.0f, y = 0.0f, z = 0.0f;
    if (view.readVec3Record(record, x, y, z)) {
        return std::array<float, 3>{x, y, z};
    }

    const std::vector<RecordField> fields =
        record_fields(bytes, view, record, dictionary);


    for (const RecordField& field : fields) {
        if (lower_ascii(field.name) != "position" ||
            (field.type != 6 && field.type != 7) || field.value == 0) {
            continue;
        }
        size_t child = 0;
        if (view.lookup(field.value, child)) {
            if (auto position = position_from_record(
                    bytes, view, child, dictionary, visited, depth + 1)) {
                return position;
            }
        }
    }
    for (const RecordField& field : fields) {
        const std::string name = lower_ascii(field.name);
        if ((field.type != 6 && field.type != 7) || field.value == 0 ||
            name == "parent" || name == "rotation") {
            continue;
        }
        size_t child = 0;
        if (view.lookup(field.value, child)) {
            if (auto position = position_from_record(
                    bytes, view, child, dictionary, visited, depth + 1)) {
                return position;
            }
        }
    }
    return std::nullopt;
}

std::optional<std::array<float, 3>> position_from_field(
    const std::vector<uint8_t>& bytes, const GdbView& view,
    const std::vector<RecordField>& fields, const std::string& field_name,
    const std::unordered_map<uint32_t, std::string>& dictionary) {
    const RecordField* field = find_field(fields, field_name);
    if (!field || (field->type != 6 && field->type != 7) ||
        field->value == 0) return std::nullopt;
    size_t record = 0;
    if (!view.lookup(field->value, record)) return std::nullopt;
    std::unordered_set<size_t> visited;
    return position_from_record(bytes, view, record, dictionary, visited, 0);
}

void append_position(CutsceneTimelineEntry& entry,
                     const std::optional<std::array<float, 3>>& position) {
    if (!position) return;
    entry.details.push_back(
        "Position: X " + format_number((*position)[0]) +
        ", Y " + format_number((*position)[1]) +
        ", Z " + format_number((*position)[2]));
}

void append_animation_metadata(CutsceneTimelineEntry& entry,
                               const std::string& animation) {
    if (!animation.empty()) {
        entry.metadata.push_back("Animation ID: " + animation);
    }
}

std::optional<CutsceneTimelineEntry> describe_timeline_element(
    const std::string& element_type, size_t record,
    const std::vector<uint8_t>& bytes, const GdbView& view,
    const std::unordered_map<uint32_t, std::string>& dictionary) {
    const std::vector<RecordField> fields =
        record_fields(bytes, view, record, dictionary);
    const std::string actor = readable_identifier(
        field_string(fields, "Character", dictionary));

    CutsceneTimelineEntry entry;




    if (element_type == "SayLine" || find_field(fields, "TextTag")) {
        entry.kind = CutsceneTimelineKind::Dialogue;
        entry.text_tag = field_string(fields, "TextTag", dictionary);
        entry.speaker = actor;
        if (entry.text_tag.empty()) return std::nullopt;
        const std::string target = readable_identifier(
            field_string(fields, "CharacterToTalkTo", dictionary));
        if (!target.empty()) entry.details.push_back("Speaking to: " + target);
        return entry;
    }

    entry.kind = CutsceneTimelineKind::ActorAction;
    if (element_type == "SetEntityMode") {
        if (actor.empty()) return std::nullopt;
        const std::string animation =
            field_string(fields, "AnimationGroup", dictionary);
        entry.description = actor + (animation.empty()
            ? " enters a scripted animation mode"
            : " starts the " + readable_identifier(animation) +
                  " animation mode");
        append_animation_metadata(entry, animation);
    } else if (element_type == "RemoveEntityMode") {
        if (actor.empty()) return std::nullopt;
        entry.description = actor + " stops the scripted animation mode";
    } else if (element_type == "PlayAnimation") {
        if (actor.empty()) return std::nullopt;
        const std::string animation =
            field_string(fields, "AnimationName", dictionary);
        entry.description = actor + " plays " +
            (animation.empty() ? std::string("a scripted animation")
                               : "the " + readable_identifier(animation) +
                                     " animation");
        append_animation_metadata(entry, animation);
        const std::string target = readable_identifier(
            field_string(fields, "CharacterToFace", dictionary));
        if (!target.empty()) entry.details.push_back("Faces: " + target);
        if (field_bool(fields, "LoopedAnim")) {
            entry.details.push_back("The animation loops until it is stopped.");
        }
    } else if (element_type == "StopLoopedAnimation") {
        if (actor.empty()) return std::nullopt;
        entry.description = actor + " stops the looping animation";
    } else if (element_type == "MoveToMarker") {
        if (actor.empty()) return std::nullopt;
        entry.description = actor +
            (field_bool(fields, "Teleport") ? " teleports to a quest marker"
                                             : " moves to a quest marker");
        append_position(entry, position_from_field(
            bytes, view, fields, "MarkerToMoveTo", dictionary));
        if (const auto range = field_float(bytes, fields, "Range")) {
            entry.details.push_back("Stops within " + format_number(*range) +
                                    " world units.");
        }
    } else if (element_type == "MoveToCharacter") {
        if (actor.empty()) return std::nullopt;
        const std::string target = readable_identifier(
            field_string(fields, "CharacterToMoveTo", dictionary));
        entry.description = actor + " moves toward " +
            (target.empty() ? std::string("another actor") : target);
        if (const auto range = field_float(bytes, fields, "Range")) {
            entry.details.push_back("Stops within " + format_number(*range) +
                                    " world units.");
        }
    } else if (element_type == "MoveToObject" ||
               element_type == "MoveToDummyObject") {
        if (actor.empty()) return std::nullopt;
        entry.description = actor + " moves to a scene object";
        const char* target_field = element_type == "MoveToObject"
            ? "ObjectToMoveTo" : "Object";
        append_position(entry, position_from_field(
            bytes, view, fields, target_field, dictionary));
    } else if (element_type == "BeginLeadPlayer") {
        if (actor.empty()) return std::nullopt;
        entry.description = actor + " starts leading Sparrow to the destination";
        append_position(entry, position_from_field(
            bytes, view, fields, "ObjectToLeadTo", dictionary));
    } else if (element_type == "EndLeadPlayer") {
        if (actor.empty()) return std::nullopt;
        entry.description = actor + " stops leading Sparrow";
    } else if (element_type == "FaceCharacter") {
        if (actor.empty()) return std::nullopt;
        const std::string target = readable_identifier(
            field_string(fields, "CharacterToFace", dictionary));
        entry.description = actor + " turns to face " +
            (target.empty() ? std::string("another actor") : target);
    } else if (element_type == "FaceObject") {
        if (actor.empty()) return std::nullopt;
        entry.description = actor + " turns to face a scene object";
        append_position(entry, position_from_field(
            bytes, view, fields, "ObjectToFace", dictionary));
    } else if (element_type == "StartLookingAtCharacter") {
        if (actor.empty()) return std::nullopt;
        const std::string target = readable_identifier(
            field_string(fields, "CharacterToLookAt", dictionary));
        entry.description = actor + " starts looking at " +
            (target.empty() ? std::string("another actor") : target);
    } else if (element_type == "StopLooking") {
        if (actor.empty()) return std::nullopt;
        entry.description = actor + " stops looking at the current target";
    } else if (element_type == "MoveIntoPositionForPairedAnim") {
        if (actor.empty()) return std::nullopt;
        const std::string second = readable_identifier(
            field_string(fields, "SecondCharacter", dictionary));
        const std::string animation =
            field_string(fields, "AnimationName", dictionary);
        const std::string second_animation =
            field_string(fields, "SecondAnimationName", dictionary);
        entry.description = actor +
            (second.empty() ? std::string() : " and " + second) +
            " move into position for a paired animation";
        append_animation_metadata(entry, animation);
        if (!second_animation.empty()) {
            entry.metadata.push_back(
                "Second animation ID: " + second_animation);
        }
    } else if (element_type == "PlayPairedAnimation") {
        if (actor.empty()) return std::nullopt;
        const std::string second = readable_identifier(
            field_string(fields, "SecondCharacter", dictionary));
        const std::string animation = readable_identifier(
            field_string(fields, "AnimationName", dictionary));
        const std::string second_animation = readable_identifier(
            field_string(fields, "SecondAnimationName", dictionary));
        entry.description = actor + " performs " +
            (animation.empty() ? std::string("a paired animation") : animation);
        if (!second.empty()) {
            entry.description += "; " + second + " performs " +
                (second_animation.empty() ? std::string("the matching animation")
                                          : second_animation);
        }
        append_animation_metadata(
            entry, field_string(fields, "AnimationName", dictionary));
        const std::string raw_second =
            field_string(fields, "SecondAnimationName", dictionary);
        if (!raw_second.empty()) {
            entry.metadata.push_back("Second animation ID: " + raw_second);
        }
    } else if (element_type == "CreateHeldObjectDuringAnim") {
        if (actor.empty()) return std::nullopt;
        const std::string object = readable_identifier(
            field_string(fields, "ObjectName", dictionary));
        const std::string animation =
            field_string(fields, "Animation", dictionary);
        entry.description = actor + " produces " +
            (object.empty() ? std::string("a held object") : object) +
            " during an animation";
        append_animation_metadata(entry, animation);
    } else if (element_type == "GiveItemToPlayer") {
        const std::string item = readable_identifier(
            field_string(fields, "Item", dictionary));
        entry.description = "Sparrow receives " +
            (item.empty() ? std::string("the quest item") : item);
    } else {



        return std::nullopt;
    }
    return entry.description.empty()
        ? std::optional<CutsceneTimelineEntry>()
        : std::optional<CutsceneTimelineEntry>(std::move(entry));
}

std::vector<CutsceneTimelineEntry> extract_timeline(
    const std::vector<uint8_t>& bytes, const GdbView& view,
    size_t root_record,
    const std::unordered_map<uint32_t, std::string>& dictionary) {
    const std::vector<RecordField> root_fields =
        record_fields(bytes, view, root_record, dictionary);
    const RecordField* elements = find_field(root_fields, "SceneElements");
    if (!elements || (elements->type != 6 && elements->type != 7) ||
        elements->value == 0) return {};
    size_t list_record = 0;
    if (!view.lookup(elements->value, list_record)) return {};

    std::vector<RecordField> list_fields =
        record_fields(bytes, view, list_record, dictionary);





    std::unordered_map<std::string, size_t> slot_counts;
    for (const RecordField& field : list_fields) {
        ++slot_counts[lower_ascii(field.name)];
    }
    std::unordered_map<std::string, std::vector<CutsceneTimelineEntry>>
        repeated_dialogue;
    for (const RecordField& field : list_fields) {
        const std::string slot_name = lower_ascii(field.name);
        if (slot_counts[slot_name] < 2 ||
            slot_name == "parent" ||
            (field.type != 6 && field.type != 7) || field.value == 0) {
            continue;
        }
        size_t element_record = 0;
        if (!view.lookup(field.value, element_record)) continue;
        auto entry = describe_timeline_element(
            field.name, element_record, bytes, view, dictionary);
        if (entry && entry->kind == CutsceneTimelineKind::Dialogue) {
            repeated_dialogue[slot_name].push_back(std::move(*entry));
        }
    }

    std::stable_sort(list_fields.begin(), list_fields.end(),
                     [](const RecordField& a, const RecordField& b) {
                         return a.order < b.order;
                     });
    std::vector<CutsceneTimelineEntry> timeline;
    std::unordered_map<std::string, size_t> next_repeated_dialogue;



    for (const RecordField& field : list_fields) {
        if (lower_ascii(field.name) == "parent" ||
            (field.type != 6 && field.type != 7) || field.value == 0) continue;
        size_t element_record = 0;
        if (!view.lookup(field.value, element_record)) continue;
        if (auto entry = describe_timeline_element(
                field.name, element_record, bytes, view, dictionary)) {
            const std::string slot_name = lower_ascii(field.name);
            const auto repeated = repeated_dialogue.find(slot_name);
            if (entry->kind == CutsceneTimelineKind::Dialogue &&
                repeated != repeated_dialogue.end()) {
                size_t& next = next_repeated_dialogue[slot_name];
                if (next < repeated->second.size()) {
                    *entry = repeated->second[next++];
                }
            }
            timeline.push_back(std::move(*entry));
        }
    }
    return timeline;
}

CutsceneReference* find_reference(
    std::unordered_map<std::string, CutsceneReference>& references,
    const std::string& id) {
    const std::string key = lower_ascii(id);
    for (auto& entry : references) {
        if (lower_ascii(entry.first) == key) return &entry.second;
    }
    return nullptr;
}

CutsceneTimelineEntry childhood_dialogue(const std::string& tag) {
    CutsceneTimelineEntry entry;
    entry.kind = CutsceneTimelineKind::Dialogue;
    entry.text_tag = tag;
    entry.speaker = "Rose";
    entry.details.push_back("Speaking to: Sparrow");
    return entry;
}

void rebuild_dialogue_index(CutsceneReference& reference) {
    reference.dialogue_lines.clear();
    reference.dialogue_tags.clear();
    for (const CutsceneTimelineEntry& entry : reference.timeline) {
        if (entry.kind != CutsceneTimelineKind::Dialogue ||
            entry.text_tag.empty()) continue;
        bool duplicate = false;
        for (const CutsceneDialogueLine& line : reference.dialogue_lines) {
            if (lower_ascii(line.text_tag) == lower_ascii(entry.text_tag)) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue;
        reference.dialogue_lines.push_back({entry.text_tag, entry.speaker});
        append_unique(reference.dialogue_tags, entry.text_tag);
        append_unique(reference.speakers, entry.speaker);
    }
}

void add_childhood_companion_dialogue(
    std::unordered_map<std::string, CutsceneReference>& references) {




    if (CutsceneReference* poo = find_reference(
            references, "QC010_RosePoo")) {
        auto insert_at = std::find_if(
            poo->timeline.begin(), poo->timeline.end(),
            [](const CutsceneTimelineEntry& entry) {
                return entry.kind == CutsceneTimelineKind::ActorAction &&
                    lower_ascii(entry.description).find(
                        "stops the scripted animation mode") !=
                        std::string::npos;
            });
        insert_at = poo->timeline.insert(
            insert_at,
            childhood_dialogue("TEXT_QUEST_QC010_ROSE_POO_10"));
        poo->timeline.insert(
            std::next(insert_at),
            childhood_dialogue("TEXT_QUEST_QC010_ROSE_POO_20"));
        rebuild_dialogue_index(*poo);
    }




    if (CutsceneReference* poo3 = find_reference(
            references, "QC010_RosePoo3")) {
        poo3->timeline.insert(
            poo3->timeline.begin(),
            childhood_dialogue("TEXT_QUEST_QC010_ROSE_POO_30"));
        rebuild_dialogue_index(*poo3);
    }
}

}

std::vector<std::string> FindCutsceneIds(const std::string& decompiled_lua) {
    std::vector<std::string> result;
    const std::vector<std::regex> patterns = {
        std::regex(R"quest(\bCutscene\s*=\s*["']([^"']+)["'])quest"),
        std::regex(R"quest(\bPlayCutscene\s*\(\s*["']([^"']+)["'])quest"),
        std::regex(R"quest(\bInteractiveCutscene\s*\(\s*["']([^"']+)["'])quest"),



        std::regex(
            R"quest(\b(?:IntroIC|WaitAroundIC|WalkAwayIC|TargetedIC|NotTargetedIC)\s*=\s*["']([^"']+)["'])quest"),
    };
    for (const std::regex& pattern : patterns) {
        for (std::sregex_iterator it(decompiled_lua.begin(), decompiled_lua.end(),
                                     pattern), finish;
             it != finish; ++it) {
            append_unique(result, (*it)[1].str());
        }
    }
    return result;
}

std::unordered_map<std::string, CutsceneReference>
ExtractCutsceneReferences(const std::vector<uint8_t>& bytes,
                          const std::vector<std::string>& cutscene_ids) {
    std::unordered_map<std::string, CutsceneReference> result;
    const GdbView view(bytes);
    if (!view.ok) return result;
    const auto dictionary = embedded_dictionary(bytes, view);
    const auto names = named_records(bytes, view);

    for (const std::string& cutscene_id : cutscene_ids) {
        size_t root_record = 0;
        bool found = false;
        const uint32_t hashes[] = {fnv1(cutscene_id), fnv1(cutscene_id, true)};
        for (uint32_t name_hash : hashes) {
            auto name = names.find(name_hash);
            if (name != names.end() && view.lookup(name->second, root_record)) {
                found = true;
                break;
            }
            if (view.lookup(name_hash, root_record)) {
                found = true;
                break;
            }
        }
        if (!found) continue;

        CutsceneReference reference;
        std::vector<size_t> pending{root_record};
        std::unordered_set<size_t> visited;
        int budget = 4096;
        while (!pending.empty() && budget-- > 0) {
            const size_t record = pending.back();
            pending.pop_back();
            if (!visited.insert(record).second) continue;

            std::vector<std::string> local_tags;
            std::vector<std::string> local_speakers;
            std::vector<std::string> local_primary_speakers;
            std::vector<size_t> local_references;
            size_t schema = 0;
            uint32_t field_count = 0;
            if (!view.schema(record, schema, field_count)) continue;
            const size_t hashes = schema + 4;
            const size_t descriptors = hashes + size_t(field_count) * 4;
            for (uint32_t i = 0; i < field_count; ++i) {
                const uint32_t field_hash =
                    ReadBeU32(bytes.data() + hashes + size_t(i) * 4);
                const uint32_t descriptor =
                    ReadBeU32(bytes.data() + descriptors + size_t(i) * 4);
                const uint8_t type = uint8_t(descriptor >> 24);
                const size_t slot = record + 4 + size_t(i) * 4;
                if (slot + 4 > view.body_end) break;
                const uint32_t value = ReadBeU32(bytes.data() + slot);

                const auto value_name = dictionary.find(value);
                if (value_name != dictionary.end()) {
                    if (looks_like_dialogue_tag(value_name->second)) {
                        append_unique(local_tags, value_name->second);
                    } else {
                        const auto field_name = dictionary.find(field_hash);
                        if (field_name != dictionary.end() &&
                            looks_like_speaker_field(field_name->second) &&
                            looks_like_speaker_value(value_name->second,
                                                     cutscene_id)) {
                            append_unique(local_speakers, value_name->second);
                            const std::string field_lower =
                                lower_ascii(field_name->second);
                            if (field_lower == "character" ||
                                field_lower == "speaker" ||
                                field_lower == "speakingcharacter") {
                                append_unique(local_primary_speakers,
                                              value_name->second);
                            }
                            append_unique(reference.speakers, value_name->second);
                        }
                    }
                }

                if ((type == 6 || type == 7) && value != 0) {
                    size_t referenced = 0;
                    if (view.lookup(value, referenced) && !visited.count(referenced)) {
                        pending.push_back(referenced);
                        local_references.push_back(referenced);
                    }
                }
            }
            if (!local_tags.empty() && local_speakers.empty()) {
                for (size_t nearby : local_references) {
                    size_t nearby_schema = 0;
                    uint32_t nearby_fields = 0;
                    if (!view.schema(nearby, nearby_schema, nearby_fields)) continue;
                    const size_t nearby_hashes = nearby_schema + 4;
                    for (uint32_t i = 0; i < nearby_fields; ++i) {
                        const uint32_t field_hash = ReadBeU32(
                            bytes.data() + nearby_hashes + size_t(i) * 4);
                        const size_t slot = nearby + 4 + size_t(i) * 4;
                        if (slot + 4 > view.body_end) break;
                        const uint32_t value = ReadBeU32(bytes.data() + slot);
                        const auto field_name = dictionary.find(field_hash);
                        const auto value_name = dictionary.find(value);
                        if (field_name != dictionary.end() &&
                            value_name != dictionary.end() &&
                            looks_like_speaker_field(field_name->second) &&
                            looks_like_speaker_value(value_name->second,
                                                     cutscene_id)) {
                            append_unique(local_speakers, value_name->second);
                        }
                    }
                }
            }
            for (const std::string& tag : local_tags) {
                bool duplicate = false;
                for (const CutsceneDialogueLine& line :
                     reference.dialogue_lines) {
                    if (lower_ascii(line.text_tag) == lower_ascii(tag)) {
                        duplicate = true;
                        break;
                    }
                }
                if (duplicate) continue;
                CutsceneDialogueLine line;
                line.text_tag = tag;
                if (local_primary_speakers.size() == 1) {
                    line.speaker = local_primary_speakers.front();
                } else if (local_speakers.size() == 1) {
                    line.speaker = local_speakers.front();
                }
                reference.dialogue_lines.push_back(std::move(line));
                append_unique(reference.dialogue_tags, tag);
            }
        }


        std::reverse(reference.dialogue_tags.begin(),
                     reference.dialogue_tags.end());
        std::reverse(reference.dialogue_lines.begin(),
                     reference.dialogue_lines.end());
        reference.timeline = extract_timeline(
            bytes, view, root_record, dictionary);
        if (!reference.timeline.empty()) {
            std::vector<CutsceneDialogueLine> ordered_lines;
            std::vector<std::string> ordered_tags;
            for (const CutsceneTimelineEntry& entry : reference.timeline) {
                if (entry.kind != CutsceneTimelineKind::Dialogue ||
                    entry.text_tag.empty()) continue;
                bool duplicate = false;
                for (const CutsceneDialogueLine& line : ordered_lines) {
                    if (lower_ascii(line.text_tag) ==
                        lower_ascii(entry.text_tag)) {
                        duplicate = true;
                        break;
                    }
                }
                if (duplicate) continue;
                ordered_lines.push_back({entry.text_tag, entry.speaker});
                append_unique(ordered_tags, entry.text_tag);
                append_unique(reference.speakers, entry.speaker);
            }
            if (!ordered_lines.empty()) {
                reference.dialogue_lines = std::move(ordered_lines);
                reference.dialogue_tags = std::move(ordered_tags);
            }
        }
        if (!reference.dialogue_tags.empty() || !reference.speakers.empty() ||
            !reference.timeline.empty()) {
            result.emplace(cutscene_id, std::move(reference));
        }
    }
    add_childhood_companion_dialogue(result);
    return result;
}

}
