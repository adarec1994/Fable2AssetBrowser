#include <vector>
#include <string>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <map>
#include <stdio.h>
#include <cstring>
#include <cmath>

// --- Third Party Includes ---
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "ImGuiFileDialog.h"

// GLFW
#include <GLFW/glfw3.h>

// =========================================================
// UTILITY FUNCTIONS
// =========================================================

inline uint32_t ReadU32BE(const uint8_t* data) {
    return (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
}

inline uint16_t ReadU16BE(const uint8_t* data) {
    return (data[0] << 8) | data[1];
}

inline int16_t ReadS16BE(const uint8_t* data) {
    uint16_t u = ReadU16BE(data);
    return *reinterpret_cast<int16_t*>(&u);
}

inline float ReadF32BE(const uint8_t* data) {
    uint32_t val = ReadU32BE(data);
    float f;
    memcpy(&f, &val, sizeof(f));
    return f;
}

// =========================================================
// DATA STRUCTURES - TOC
// =========================================================

struct AnimEvent {
    float time = 0.0f;
    uint32_t string_index = 0;
    std::string string_value;
    uint32_t next_index = 0;
};

struct AnimEntry {
    int index = 0;
    uint32_t toc_offset = 0;
    uint32_t data_offset = 0;
    uint32_t field1 = 0;        // Matches DATA.unk2
    float fps = 30.0f;
    uint32_t event_count_field = 0;
    uint64_t hash_value = 0;
    std::vector<AnimEvent> events;

    // Cross-reference to DATA clip
    int linked_clip_index = -1;
};

struct TocHeader {
    char magic[9] = {0};
    uint32_t version = 0;
    uint32_t section_count = 0;
    uint32_t anim_count = 0;
    uint32_t field4 = 0;
    uint32_t string_count = 0;
};

// =========================================================
// DATA STRUCTURES - ANIMATION DATA
// =========================================================

struct Quaternion {
    float w = 1.0f, x = 0.0f, y = 0.0f, z = 0.0f;

    float Length() const {
        return std::sqrt(w*w + x*x + y*y + z*z);
    }

    void Normalize() {
        float len = Length();
        if (len > 0.0001f) {
            w /= len; x /= len; y /= len; z /= len;
        }
    }

    bool IsValid() const {
        float len = Length();
        return len > 0.99f && len < 1.01f;
    }
};

struct Keyframe {
    uint32_t byte_offset = 0;
    float time = 0.0f;
    Quaternion rotation;
    std::vector<float> raw_values;  // All extracted values from block
};

struct BoneTrack {
    int bone_index = 0;
    std::vector<Keyframe> keyframes;
};

struct AnimClip {
    int index = 0;
    uint32_t file_offset = 0;
    uint32_t size = 0;

    // Header fields
    uint32_t magic = 0;
    uint32_t bone_count = 0;
    uint32_t frame_count = 0;
    uint32_t offset_table_size = 0;  // unk1
    uint32_t unk2 = 0;               // Matches TOC.field1
    uint32_t track_count = 0;

    // Extracted data
    std::vector<BoneTrack> tracks;

    // Cross-reference to TOC
    int linked_toc_index = -1;

    float Duration() const { return frame_count / 30.0f; }
};

// =========================================================
// FABLE 2 ANIMATION PARSER
// =========================================================

class Fable2AnimParser {
public:
    std::vector<uint8_t> toc_data;
    std::vector<uint8_t> data_file;

    TocHeader header;
    std::vector<std::string> strings;
    std::vector<AnimEntry> entries;
    std::vector<AnimClip> clips;
    std::string load_error;

    static constexpr uint32_t FPS_MARKER = 0x41F00000;  // 30.0 as BE float
    static constexpr uint32_t CLIP_MAGIC = 0xCEA5EBED;

    bool LoadToc(const std::string& tocPath) {
        entries.clear();
        strings.clear();
        load_error = "";

        if (!ReadFile(tocPath, toc_data)) {
            load_error = "Failed to read TOC file.";
            return false;
        }

        if (!ParseHeader()) {
            return false;
        }

        ParseStrings();
        ParseEntries();
        return true;
    }

    bool LoadData(const std::string& dataPath) {
        clips.clear();

        if (!ReadFile(dataPath, data_file)) {
            load_error = "Failed to read DATA file.";
            return false;
        }

        ParseClips();
        CrossReferenceEntries();
        return true;
    }

    std::string GetString(uint32_t index) const {
        if (index < strings.size()) {
            return strings[index];
        }
        return "<invalid:" + std::to_string(index) + ">";
    }

    // Extract keyframe data for a clip
    bool ExtractClipKeyframes(int clipIndex) {
        if (clipIndex < 0 || clipIndex >= (int)clips.size()) {
            return false;
        }

        AnimClip& clip = clips[clipIndex];

        // Already extracted?
        if (!clip.tracks.empty()) {
            return true;
        }

        if (clip.track_count > 1) {
            return ExtractMultiTrackClip(clip);
        } else {
            return ExtractSingleTrackClip(clip);
        }
    }

private:
    bool ReadFile(const std::string& path, std::vector<uint8_t>& outData) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) return false;
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        outData.resize(size);
        if (file.read((char*)outData.data(), size)) return true;
        return false;
    }

    bool ParseHeader() {
        if (toc_data.size() < 28) {
            load_error = "TOC file too small for header.";
            return false;
        }

        if (memcmp(toc_data.data(), "AnimBank", 8) != 0) {
            load_error = "Invalid magic - expected 'AnimBank'.";
            return false;
        }

        memcpy(header.magic, toc_data.data(), 8);
        header.version = ReadU32BE(&toc_data[8]);
        header.section_count = ReadU32BE(&toc_data[12]);
        header.anim_count = ReadU32BE(&toc_data[16]);
        header.field4 = ReadU32BE(&toc_data[20]);
        header.string_count = ReadU32BE(&toc_data[24]);

        return true;
    }

    size_t FindEntryTableStart() {
        for (size_t i = 8; i < toc_data.size() - 4; i++) {
            if (ReadU32BE(&toc_data[i]) == FPS_MARKER) {
                return i - 8;
            }
        }
        return toc_data.size();
    }

    void ParseStrings() {
        size_t entry_table_start = FindEntryTableStart();
        size_t pos = 0x1A;

        while (pos < entry_table_start) {
            if (toc_data[pos] == 0) {
                pos++;
                continue;
            }

            size_t end = pos;
            while (end < toc_data.size() && toc_data[end] != 0) {
                end++;
            }

            std::string s(reinterpret_cast<char*>(&toc_data[pos]), end - pos);
            strings.push_back(s);
            pos = end + 1;
        }
    }

    void ParseEntries() {
        size_t entry_table_start = FindEntryTableStart();
        size_t pos = entry_table_start;
        int anim_index = 0;

        while (pos + 24 <= toc_data.size()) {
            if (ReadU32BE(&toc_data[pos + 8]) == FPS_MARKER) {
                AnimEntry entry;
                entry.index = anim_index++;
                entry.toc_offset = (uint32_t)pos;
                entry.data_offset = ReadU32BE(&toc_data[pos]);
                entry.field1 = ReadU32BE(&toc_data[pos + 4]);
                entry.fps = 30.0f;
                entry.event_count_field = ReadU32BE(&toc_data[pos + 12]);

                uint32_t hash_high = ReadU32BE(&toc_data[pos + 16]);
                uint32_t hash_low = ReadU32BE(&toc_data[pos + 20]);
                entry.hash_value = ((uint64_t)hash_high << 32) | hash_low;

                pos += 24;

                while (pos + 24 <= toc_data.size()) {
                    if (ReadU32BE(&toc_data[pos + 8]) == FPS_MARKER) {
                        break;
                    }

                    uint32_t string_idx = ReadU32BE(&toc_data[pos]);
                    float time = ReadF32BE(&toc_data[pos + 4]);
                    uint32_t next_idx = ReadU32BE(&toc_data[pos + 8]);

                    if (time >= 0 && time <= 10.0f && string_idx < strings.size() && next_idx < 10000) {
                        AnimEvent evt;
                        evt.time = time;
                        evt.string_index = string_idx;
                        evt.string_value = GetString(string_idx);
                        evt.next_index = next_idx;
                        entry.events.push_back(evt);
                    }

                    pos += 24;
                }

                entries.push_back(entry);
            } else {
                pos += 24;
            }
        }
    }

    // =========================================================
    // DATA FILE PARSING
    // =========================================================

    void ParseClips() {
        // Find all clip magic markers
        std::vector<size_t> clip_offsets;

        for (size_t i = 0; i + 4 <= data_file.size(); i++) {
            if (ReadU32BE(&data_file[i]) == CLIP_MAGIC) {
                clip_offsets.push_back(i);
            }
        }

        // Parse each clip header
        for (size_t i = 0; i < clip_offsets.size(); i++) {
            size_t offset = clip_offsets[i];
            size_t next_offset = (i + 1 < clip_offsets.size()) ? clip_offsets[i + 1] : data_file.size();

            if (offset + 28 > data_file.size()) continue;

            AnimClip clip;
            clip.index = (int)i;
            clip.file_offset = (uint32_t)offset;
            clip.size = (uint32_t)(next_offset - offset);

            clip.magic = ReadU32BE(&data_file[offset]);
            clip.bone_count = ReadU32BE(&data_file[offset + 4]);
            clip.frame_count = ReadU32BE(&data_file[offset + 8]);
            clip.offset_table_size = ReadU32BE(&data_file[offset + 12]);
            clip.unk2 = ReadU32BE(&data_file[offset + 16]);
            clip.track_count = ReadU32BE(&data_file[offset + 20]);

            clips.push_back(clip);
        }
    }

    void CrossReferenceEntries() {
        // Build lookup from unk2 to clip index
        std::map<uint32_t, int> unk2_to_clip;
        for (size_t i = 0; i < clips.size(); i++) {
            unk2_to_clip[clips[i].unk2] = (int)i;
        }

        // Link TOC entries to clips via field1 == unk2
        for (auto& entry : entries) {
            auto it = unk2_to_clip.find(entry.field1);
            if (it != unk2_to_clip.end()) {
                entry.linked_clip_index = it->second;
                clips[it->second].linked_toc_index = entry.index;
            }
        }
    }

    // =========================================================
    // KEYFRAME EXTRACTION
    // =========================================================

    Quaternion ExtractQuaternion(size_t offset) {
        Quaternion q;
        if (offset + 8 > data_file.size()) {
            return q;
        }

        // Read 4 x int16 BE, normalize to [-1, 1]
        q.w = ReadS16BE(&data_file[offset + 0]) / 32767.0f;
        q.x = ReadS16BE(&data_file[offset + 2]) / 32767.0f;
        q.y = ReadS16BE(&data_file[offset + 4]) / 32767.0f;
        q.z = ReadS16BE(&data_file[offset + 6]) / 32767.0f;

        q.Normalize();
        return q;
    }

    std::vector<float> ExtractRawValues(size_t offset, int count) {
        std::vector<float> values;
        for (int i = 0; i < count && offset + i * 2 + 2 <= data_file.size(); i++) {
            float v = ReadS16BE(&data_file[offset + i * 2]) / 32767.0f;
            values.push_back(v);
        }
        return values;
    }

    bool ExtractMultiTrackClip(AnimClip& clip) {
        size_t clip_start = clip.file_offset;
        size_t clip_end = clip_start + clip.size;

        clip.tracks.clear();

        for (uint32_t track_idx = 0; track_idx < clip.track_count; track_idx++) {
            BoneTrack track;
            track.bone_index = track_idx;

            // Get track offset from header
            size_t track_offset_pos = clip_start + 28 + track_idx * 4;
            if (track_offset_pos + 4 > data_file.size()) break;

            uint32_t track_rel_offset = ReadU32BE(&data_file[track_offset_pos]);
            size_t track_abs = clip_start + track_rel_offset;

            // Calculate track size
            uint32_t next_track_rel = 0;
            if (track_idx + 1 < clip.track_count) {
                next_track_rel = ReadU32BE(&data_file[track_offset_pos + 4]);
            } else {
                next_track_rel = clip.size;
            }
            uint32_t track_size = next_track_rel - track_rel_offset;

            // Read offset table (absolute file offsets)
            uint32_t num_offsets = track_size / 4;
            std::vector<uint32_t> kf_offsets;

            for (uint32_t i = 0; i < num_offsets; i++) {
                size_t off_pos = track_abs + i * 4;
                if (off_pos + 4 > data_file.size()) break;

                uint32_t off = ReadU32BE(&data_file[off_pos]);
                if (off > 0 && off < data_file.size()) {
                    kf_offsets.push_back(off);
                } else {
                    break;  // End of valid offsets
                }
            }

            // Extract keyframes
            float duration = clip.Duration();
            for (size_t kf_idx = 0; kf_idx < kf_offsets.size(); kf_idx++) {
                Keyframe kf;
                kf.byte_offset = kf_offsets[kf_idx];
                kf.time = (kf_offsets.size() > 1) ?
                    (kf_idx / (float)(kf_offsets.size() - 1)) * duration : 0.0f;
                kf.rotation = ExtractQuaternion(kf_offsets[kf_idx]);
                kf.raw_values = ExtractRawValues(kf_offsets[kf_idx], 8);

                track.keyframes.push_back(kf);
            }

            clip.tracks.push_back(track);
        }

        return true;
    }

    bool ExtractSingleTrackClip(AnimClip& clip) {
        size_t clip_start = clip.file_offset;

        // Get track 0 offset
        size_t track_offset_pos = clip_start + 28;
        if (track_offset_pos + 4 > data_file.size()) return false;

        uint32_t track_rel = ReadU32BE(&data_file[track_offset_pos]);
        size_t track_abs = clip_start + track_rel;

        // Read offset table
        std::vector<uint32_t> offset_table;
        for (uint32_t i = 0; i < clip.offset_table_size; i++) {
            size_t pos = track_abs + i * 4;
            if (pos + 4 > data_file.size()) break;
            offset_table.push_back(ReadU32BE(&data_file[pos]));
        }

        // Compressed data base (after offset table + 4 byte keyframe header)
        size_t compressed_base = track_abs + clip.offset_table_size * 4 + 4;

        clip.tracks.clear();
        float duration = clip.Duration();

        // Parse bone structure from offset table
        for (uint32_t bone_idx = 0; bone_idx < clip.bone_count; bone_idx++) {
            BoneTrack track;
            track.bone_index = bone_idx;

            // First bone_count entries are bone-level indices
            uint32_t start_entry_idx = offset_table[bone_idx] / 4;
            uint32_t end_entry_idx;

            if (bone_idx + 1 < clip.bone_count) {
                end_entry_idx = offset_table[bone_idx + 1] / 4;
            } else {
                // Find first entry beyond table size
                uint32_t table_size_bytes = clip.offset_table_size * 4;
                end_entry_idx = clip.offset_table_size;
                for (uint32_t i = bone_idx + 1; i < clip.offset_table_size; i++) {
                    if (offset_table[i] >= table_size_bytes) {
                        break;
                    }
                    end_entry_idx = i + 1;
                }
            }

            // Get keyframe offsets for this bone
            for (uint32_t entry_idx = start_entry_idx; entry_idx < end_entry_idx; entry_idx++) {
                if (entry_idx >= offset_table.size()) break;

                uint32_t kf_byte_offset = offset_table[entry_idx];
                size_t kf_abs = compressed_base + kf_byte_offset;

                Keyframe kf;
                kf.byte_offset = kf_byte_offset;

                uint32_t num_kf = end_entry_idx - start_entry_idx;
                uint32_t kf_local_idx = entry_idx - start_entry_idx;
                kf.time = (num_kf > 1) ? (kf_local_idx / (float)(num_kf - 1)) * duration : 0.0f;

                kf.rotation = ExtractQuaternion(kf_abs);
                kf.raw_values = ExtractRawValues(kf_abs, 8);

                track.keyframes.push_back(kf);
            }

            clip.tracks.push_back(track);
        }

        return true;
    }

public:
    // =========================================================
    // EXPORT FUNCTIONS
    // =========================================================

    bool ExportClipToJson(int clipIndex, const std::string& outputPath) {
        if (clipIndex < 0 || clipIndex >= (int)clips.size()) {
            return false;
        }

        ExtractClipKeyframes(clipIndex);
        const AnimClip& clip = clips[clipIndex];

        std::ofstream out(outputPath);
        if (!out.is_open()) return false;

        out << "{\n";
        out << "  \"clip_index\": " << clip.index << ",\n";
        out << "  \"file_offset\": \"0x" << std::hex << clip.file_offset << std::dec << "\",\n";
        out << "  \"bone_count\": " << clip.bone_count << ",\n";
        out << "  \"frame_count\": " << clip.frame_count << ",\n";
        out << "  \"track_count\": " << clip.track_count << ",\n";
        out << "  \"duration\": " << std::fixed << std::setprecision(4) << clip.Duration() << ",\n";
        out << "  \"linked_toc_index\": " << clip.linked_toc_index << ",\n";
        out << "  \"tracks\": [\n";

        for (size_t t = 0; t < clip.tracks.size(); t++) {
            const BoneTrack& track = clip.tracks[t];
            out << "    {\n";
            out << "      \"bone_index\": " << track.bone_index << ",\n";
            out << "      \"keyframe_count\": " << track.keyframes.size() << ",\n";
            out << "      \"keyframes\": [\n";

            for (size_t k = 0; k < track.keyframes.size(); k++) {
                const Keyframe& kf = track.keyframes[k];
                out << "        {\n";
                out << "          \"time\": " << std::fixed << std::setprecision(4) << kf.time << ",\n";
                out << "          \"quaternion\": ["
                    << kf.rotation.w << ", " << kf.rotation.x << ", "
                    << kf.rotation.y << ", " << kf.rotation.z << "]\n";
                out << "        }" << (k + 1 < track.keyframes.size() ? "," : "") << "\n";
            }

            out << "      ]\n";
            out << "    }" << (t + 1 < clip.tracks.size() ? "," : "") << "\n";
        }

        out << "  ]\n";
        out << "}\n";

        return true;
    }

    bool ExportAllClipsToJson(const std::string& outputPath, int maxClips = -1) {
        std::ofstream out(outputPath);
        if (!out.is_open()) return false;

        int count = (maxClips > 0) ? std::min(maxClips, (int)clips.size()) : (int)clips.size();

        out << "{\n";
        out << "  \"clip_count\": " << count << ",\n";
        out << "  \"clips\": [\n";

        for (int i = 0; i < count; i++) {
            ExtractClipKeyframes(i);
            const AnimClip& clip = clips[i];

            out << "    {\n";
            out << "      \"index\": " << clip.index << ",\n";
            out << "      \"offset\": \"0x" << std::hex << clip.file_offset << std::dec << "\",\n";
            out << "      \"bones\": " << clip.bone_count << ",\n";
            out << "      \"frames\": " << clip.frame_count << ",\n";
            out << "      \"tracks\": " << clip.track_count << ",\n";
            out << "      \"unk2\": " << clip.unk2 << ",\n";
            out << "      \"linked_toc\": " << clip.linked_toc_index << ",\n";
            out << "      \"keyframe_count\": ";

            int total_kf = 0;
            for (const auto& t : clip.tracks) {
                total_kf += (int)t.keyframes.size();
            }
            out << total_kf << ",\n";

            // Sample quaternion from first track
            out << "      \"sample_quat\": ";
            if (!clip.tracks.empty() && !clip.tracks[0].keyframes.empty()) {
                const Quaternion& q = clip.tracks[0].keyframes[0].rotation;
                out << "[" << std::fixed << std::setprecision(4)
                    << q.w << ", " << q.x << ", " << q.y << ", " << q.z << "]";
            } else {
                out << "[1, 0, 0, 0]";
            }
            out << "\n";

            out << "    }" << (i + 1 < count ? "," : "") << "\n";
        }

        out << "  ]\n";
        out << "}\n";

        return true;
    }
};

// =========================================================
// GUI APPLICATION CLASS
// =========================================================

class FableAnimApp {
private:
    Fable2AnimParser parser;
    std::string tocPath;
    std::string dataPath;
    std::string logBuffer;

    // Search/Filter
    char filterBuffer[128] = "";
    char eventFilterBuffer[128] = "";
    char clipFilterBuffer[128] = "";

    // Selection
    int selectedEntry = -1;
    int selectedClip = -1;

    // Export
    char exportPath[256] = "exported_animation.json";

    void Log(const std::string& msg) {
        logBuffer += msg + "\n";
        std::cout << "[LOG] " << msg << std::endl;
    }

public:
    void Render() {
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                      ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

        if (ImGui::Begin("Fable 2 Animation Viewer", nullptr, window_flags)) {

            // --- Header ---
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "FABLE 2 ANIMATION VIEWER (with Decompression)");
            ImGui::Separator();

            // File selection row
            ImGui::Text("TOC:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(300);
            ImGui::InputText("##toc", const_cast<char*>(tocPath.c_str()), tocPath.capacity() + 1, ImGuiInputTextFlags_ReadOnly);
            ImGui::SameLine();
            if (ImGui::Button("Browse TOC")) {
                IGFD::FileDialogConfig config;
                config.path = ".";
                IGFD::FileDialog::Instance()->OpenDialog("ChooseTocDlg", "Choose TOC File", ".animation_toc,.*", config);
            }

            ImGui::SameLine();
            ImGui::Text("DATA:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(300);
            ImGui::InputText("##data", const_cast<char*>(dataPath.c_str()), dataPath.capacity() + 1, ImGuiInputTextFlags_ReadOnly);
            ImGui::SameLine();
            if (ImGui::Button("Browse DATA")) {
                IGFD::FileDialogConfig config;
                config.path = ".";
                IGFD::FileDialog::Instance()->OpenDialog("ChooseDataDlg", "Choose DATA File", ".animation_data,.*", config);
            }

            ImGui::SameLine();
            if (ImGui::Button("PARSE")) {
                if (tocPath.empty() && dataPath.empty()) {
                    Log("Error: No files selected.");
                } else {
                    if (!tocPath.empty()) {
                        Log("Parsing TOC...");
                        if (parser.LoadToc(tocPath)) {
                            Log("TOC: " + std::to_string(parser.entries.size()) + " entries, "
                                + std::to_string(parser.strings.size()) + " strings.");
                        } else {
                            Log("TOC Error: " + parser.load_error);
                        }
                    }

                    if (!dataPath.empty()) {
                        Log("Parsing DATA...");
                        if (parser.LoadData(dataPath)) {
                            Log("DATA: " + std::to_string(parser.clips.size()) + " clips, "
                                + std::to_string(parser.data_file.size()) + " bytes.");

                            // Count cross-references
                            int linked = 0;
                            for (const auto& e : parser.entries) {
                                if (e.linked_clip_index >= 0) linked++;
                            }
                            Log("Cross-referenced " + std::to_string(linked) + " TOC entries to DATA clips.");
                        } else {
                            Log("DATA Error: " + parser.load_error);
                        }
                    }
                }
            }

            ImGui::Separator();

            // --- Stats ---
            if (!parser.entries.empty() || !parser.clips.empty()) {
                ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f),
                    "TOC: %d entries, %d strings | DATA: %d clips",
                    (int)parser.entries.size(), (int)parser.strings.size(), (int)parser.clips.size());
                ImGui::Separator();
            }

            // --- View Mode Tabs ---
            ImGui::BeginTabBar("ViewTabs");

            if (ImGui::BeginTabItem("TOC Entries")) {
                RenderEntriesView();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("DATA Clips")) {
                RenderClipsView();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("String Table")) {
                RenderStringsView();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Selected Entry")) {
                RenderSelectedEntryView();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Selected Clip")) {
                RenderSelectedClipView();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Export")) {
                RenderExportView();
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();

            // --- Footer Log ---
            ImGui::Separator();
            ImGui::Text("Log:");
            ImGui::BeginChild("LogRegion", ImVec2(0, 80), true);
            ImGui::TextUnformatted(logBuffer.c_str());
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                ImGui::SetScrollHereY(1.0f);
            ImGui::EndChild();
        }
        ImGui::End();

        // Dialog handling
        HandleFileDialogs();
    }

private:
    void HandleFileDialogs() {
        ImVec2 minSize = ImVec2(600, 400);
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse;

        if (IGFD::FileDialog::Instance()->Display("ChooseTocDlg", flags, minSize)) {
            if (IGFD::FileDialog::Instance()->IsOk()) {
                tocPath = IGFD::FileDialog::Instance()->GetFilePathName();
                Log("Selected TOC: " + tocPath);

                std::string basePath = tocPath;
                size_t tocPos = basePath.find(".animation_toc");
                if (tocPos != std::string::npos) {
                    std::string dataCandidate = basePath.substr(0, tocPos) + ".animation_data";
                    if (std::filesystem::exists(dataCandidate)) {
                        dataPath = dataCandidate;
                        Log("Auto-detected DATA file: " + dataPath);
                    }
                }
            }
            IGFD::FileDialog::Instance()->Close();
        }

        if (IGFD::FileDialog::Instance()->Display("ChooseDataDlg", flags, minSize)) {
            if (IGFD::FileDialog::Instance()->IsOk()) {
                dataPath = IGFD::FileDialog::Instance()->GetFilePathName();
                Log("Selected DATA: " + dataPath);
            }
            IGFD::FileDialog::Instance()->Close();
        }
    }

    void RenderEntriesView() {
        ImGui::Text("Filter:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200);
        ImGui::InputText("##filter", filterBuffer, IM_ARRAYSIZE(filterBuffer));
        ImGui::SameLine();
        ImGui::Text("Event:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150);
        ImGui::InputText("##eventFilter", eventFilterBuffer, IM_ARRAYSIZE(eventFilterBuffer));

        std::vector<const AnimEntry*> displayList;
        std::string filterStr = filterBuffer;
        std::string eventFilterStr = eventFilterBuffer;

        for (const auto& entry : parser.entries) {
            bool passesFilter = true;

            if (!eventFilterStr.empty()) {
                bool hasEvent = false;
                for (const auto& evt : entry.events) {
                    if (evt.string_value.find(eventFilterStr) != std::string::npos) {
                        hasEvent = true;
                        break;
                    }
                }
                if (!hasEvent) passesFilter = false;
            }

            if (passesFilter && !filterStr.empty()) {
                bool matches = false;
                char hashStr[32];
                snprintf(hashStr, sizeof(hashStr), "%016llX", (unsigned long long)entry.hash_value);
                if (std::string(hashStr).find(filterStr) != std::string::npos) {
                    matches = true;
                }
                for (const auto& evt : entry.events) {
                    if (evt.string_value.find(filterStr) != std::string::npos) {
                        matches = true;
                        break;
                    }
                }
                if (!matches) passesFilter = false;
            }

            if (passesFilter) {
                displayList.push_back(&entry);
            }
        }

        ImGui::Text("Showing %d / %d entries", (int)displayList.size(), (int)parser.entries.size());

        if (ImGui::BeginTable("EntriesTable", 7,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable)) {

            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Idx", ImGuiTableColumnFlags_WidthFixed, 50);
            ImGui::TableSetupColumn("Data Off", ImGuiTableColumnFlags_WidthFixed, 80);
            ImGui::TableSetupColumn("Field1", ImGuiTableColumnFlags_WidthFixed, 50);
            ImGui::TableSetupColumn("Clip#", ImGuiTableColumnFlags_WidthFixed, 50);
            ImGui::TableSetupColumn("Hash", ImGuiTableColumnFlags_WidthFixed, 140);
            ImGui::TableSetupColumn("Evts", ImGuiTableColumnFlags_WidthFixed, 40);
            ImGui::TableSetupColumn("Event Preview", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            ImGuiListClipper clipper;
            clipper.Begin((int)displayList.size());
            while (clipper.Step()) {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                    const AnimEntry* entry = displayList[i];

                    ImGui::TableNextRow();
                    bool isSelected = (selectedEntry == entry->index);

                    ImGui::TableSetColumnIndex(0);
                    char label[32];
                    snprintf(label, sizeof(label), "%d", entry->index);
                    if (ImGui::Selectable(label, isSelected, ImGuiSelectableFlags_SpanAllColumns)) {
                        selectedEntry = entry->index;
                        if (entry->linked_clip_index >= 0) {
                            selectedClip = entry->linked_clip_index;
                        }
                    }

                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "0x%06X", entry->data_offset);

                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%d", entry->field1);

                    ImGui::TableSetColumnIndex(3);
                    if (entry->linked_clip_index >= 0) {
                        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "%d", entry->linked_clip_index);
                    } else {
                        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "-");
                    }

                    ImGui::TableSetColumnIndex(4);
                    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.5f, 1.0f), "%016llX",
                        (unsigned long long)entry->hash_value);

                    ImGui::TableSetColumnIndex(5);
                    if (entry->events.empty()) {
                        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "0");
                    } else {
                        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "%d", (int)entry->events.size());
                    }

                    ImGui::TableSetColumnIndex(6);
                    if (!entry->events.empty()) {
                        std::string preview;
                        int shown = 0;
                        for (const auto& evt : entry->events) {
                            if (shown >= 3) { preview += "..."; break; }
                            if (!preview.empty()) preview += ", ";
                            std::string evtStr = evt.string_value;
                            if (evtStr.length() > 25) evtStr = evtStr.substr(0, 22) + "...";
                            preview += evtStr;
                            shown++;
                        }
                        ImGui::TextUnformatted(preview.c_str());
                    }
                }
            }
            ImGui::EndTable();
        }
    }

    void RenderClipsView() {
        ImGui::Text("Filter:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200);
        ImGui::InputText("##clipFilter", clipFilterBuffer, IM_ARRAYSIZE(clipFilterBuffer));

        ImGui::SameLine();
        ImGui::Text("  Tracks: ");
        static int trackFilter = 0;  // 0 = all, 1-10 = specific
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80);
        ImGui::InputInt("##trackFilter", &trackFilter);
        if (trackFilter < 0) trackFilter = 0;

        std::vector<const AnimClip*> displayList;

        for (const auto& clip : parser.clips) {
            bool passes = true;

            if (trackFilter > 0 && clip.track_count != (uint32_t)trackFilter) {
                passes = false;
            }

            if (passes) {
                displayList.push_back(&clip);
            }
        }

        ImGui::Text("Showing %d / %d clips", (int)displayList.size(), (int)parser.clips.size());

        if (ImGui::BeginTable("ClipsTable", 8,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_Resizable)) {

            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Idx", ImGuiTableColumnFlags_WidthFixed, 50);
            ImGui::TableSetupColumn("Offset", ImGuiTableColumnFlags_WidthFixed, 80);
            ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 70);
            ImGui::TableSetupColumn("Bones", ImGuiTableColumnFlags_WidthFixed, 50);
            ImGui::TableSetupColumn("Frames", ImGuiTableColumnFlags_WidthFixed, 60);
            ImGui::TableSetupColumn("Tracks", ImGuiTableColumnFlags_WidthFixed, 55);
            ImGui::TableSetupColumn("unk2", ImGuiTableColumnFlags_WidthFixed, 50);
            ImGui::TableSetupColumn("TOC#", ImGuiTableColumnFlags_WidthFixed, 50);
            ImGui::TableHeadersRow();

            ImGuiListClipper clipper;
            clipper.Begin((int)displayList.size());
            while (clipper.Step()) {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                    const AnimClip* clip = displayList[i];

                    ImGui::TableNextRow();
                    bool isSelected = (selectedClip == clip->index);

                    ImGui::TableSetColumnIndex(0);
                    char label[32];
                    snprintf(label, sizeof(label), "%d", clip->index);
                    if (ImGui::Selectable(label, isSelected, ImGuiSelectableFlags_SpanAllColumns)) {
                        selectedClip = clip->index;
                        if (clip->linked_toc_index >= 0) {
                            selectedEntry = clip->linked_toc_index;
                        }
                    }

                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "0x%06X", clip->file_offset);

                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%d", clip->size);

                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%d", clip->bone_count);

                    ImGui::TableSetColumnIndex(4);
                    ImGui::Text("%d", clip->frame_count);

                    ImGui::TableSetColumnIndex(5);
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.5f, 1.0f), "%d", clip->track_count);

                    ImGui::TableSetColumnIndex(6);
                    ImGui::Text("%d", clip->unk2);

                    ImGui::TableSetColumnIndex(7);
                    if (clip->linked_toc_index >= 0) {
                        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "%d", clip->linked_toc_index);
                    } else {
                        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "-");
                    }
                }
            }
            ImGui::EndTable();
        }
    }

    void RenderStringsView() {
        ImGui::Text("Filter:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(300);
        static char strFilter[128] = "";
        ImGui::InputText("##strfilter", strFilter, IM_ARRAYSIZE(strFilter));

        std::string filterStr = strFilter;
        std::vector<std::pair<int, const std::string*>> displayList;

        for (size_t i = 0; i < parser.strings.size(); i++) {
            if (filterStr.empty() || parser.strings[i].find(filterStr) != std::string::npos) {
                displayList.push_back({(int)i, &parser.strings[i]});
            }
        }

        ImGui::Text("Showing %d / %d strings", (int)displayList.size(), (int)parser.strings.size());

        if (ImGui::BeginTable("StringsTable", 2,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable)) {

            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Index", ImGuiTableColumnFlags_WidthFixed, 70);
            ImGui::TableSetupColumn("String", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            ImGuiListClipper clipper;
            clipper.Begin((int)displayList.size());
            while (clipper.Step()) {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextColored(ImVec4(0, 1, 1, 1), "%d", displayList[i].first);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(displayList[i].second->c_str());
                }
            }
            ImGui::EndTable();
        }
    }

    void RenderSelectedEntryView() {
        if (selectedEntry < 0 || selectedEntry >= (int)parser.entries.size()) {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                "Select an entry from the TOC Entries tab.");
            return;
        }

        const AnimEntry& entry = parser.entries[selectedEntry];

        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "TOC Entry %d", entry.index);
        ImGui::Separator();

        ImGui::Columns(2, "entrydetails", true);
        ImGui::SetColumnWidth(0, 180);

        ImGui::Text("TOC Offset:"); ImGui::NextColumn();
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "0x%05X", entry.toc_offset); ImGui::NextColumn();

        ImGui::Text("Data Offset:"); ImGui::NextColumn();
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "0x%06X", entry.data_offset); ImGui::NextColumn();

        ImGui::Text("Field1 (unk2):"); ImGui::NextColumn();
        ImGui::Text("%d", entry.field1); ImGui::NextColumn();

        ImGui::Text("Linked Clip:"); ImGui::NextColumn();
        if (entry.linked_clip_index >= 0) {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "%d", entry.linked_clip_index);
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "Not found");
        }
        ImGui::NextColumn();

        ImGui::Text("Hash:"); ImGui::NextColumn();
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.5f, 1.0f), "%016llX", (unsigned long long)entry.hash_value); ImGui::NextColumn();

        ImGui::Text("Events:"); ImGui::NextColumn();
        ImGui::Text("%d", (int)entry.events.size()); ImGui::NextColumn();

        ImGui::Columns(1);

        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Events Timeline:");

        if (entry.events.empty()) {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "(No events)");
        } else {
            if (ImGui::BeginTable("EventsTable", 3,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                ImVec2(0, 250))) {

                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Time (s)", ImGuiTableColumnFlags_WidthFixed, 80);
                ImGui::TableSetupColumn("Idx", ImGuiTableColumnFlags_WidthFixed, 50);
                ImGui::TableSetupColumn("Event", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();

                for (const auto& evt : entry.events) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "%.3f", evt.time);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%d", evt.string_index);
                    ImGui::TableSetColumnIndex(2);

                    ImVec4 color(1, 1, 1, 1);
                    if (evt.string_value.find("SE_") != std::string::npos) {
                        color = ImVec4(0.5f, 0.8f, 1.0f, 1.0f);
                    } else if (evt.string_value.find("COMBAT") != std::string::npos) {
                        color = ImVec4(1.0f, 0.5f, 0.5f, 1.0f);
                    } else if (evt.string_value.find("IK_TARGET") != std::string::npos) {
                        color = ImVec4(1.0f, 0.8f, 0.5f, 1.0f);
                    } else if (evt.string_value.find("FOOT") != std::string::npos) {
                        color = ImVec4(0.5f, 1.0f, 0.5f, 1.0f);
                    }
                    ImGui::TextColored(color, "%s", evt.string_value.c_str());
                }
                ImGui::EndTable();
            }
        }
    }

    void RenderSelectedClipView() {
        if (selectedClip < 0 || selectedClip >= (int)parser.clips.size()) {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                "Select a clip from the DATA Clips tab.");
            return;
        }

        // Extract keyframes on demand
        parser.ExtractClipKeyframes(selectedClip);
        const AnimClip& clip = parser.clips[selectedClip];

        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "DATA Clip %d", clip.index);
        ImGui::Separator();

        // Header info
        ImGui::Columns(2, "clipdetails", true);
        ImGui::SetColumnWidth(0, 150);

        ImGui::Text("File Offset:"); ImGui::NextColumn();
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "0x%06X", clip.file_offset); ImGui::NextColumn();

        ImGui::Text("Size:"); ImGui::NextColumn();
        ImGui::Text("%d bytes", clip.size); ImGui::NextColumn();

        ImGui::Text("Bones:"); ImGui::NextColumn();
        ImGui::Text("%d", clip.bone_count); ImGui::NextColumn();

        ImGui::Text("Frames:"); ImGui::NextColumn();
        ImGui::Text("%d", clip.frame_count); ImGui::NextColumn();

        ImGui::Text("Tracks:"); ImGui::NextColumn();
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.5f, 1.0f), "%d", clip.track_count); ImGui::NextColumn();

        ImGui::Text("Duration:"); ImGui::NextColumn();
        ImGui::Text("%.2f sec", clip.Duration()); ImGui::NextColumn();

        ImGui::Text("unk2:"); ImGui::NextColumn();
        ImGui::Text("%d", clip.unk2); ImGui::NextColumn();

        ImGui::Text("Linked TOC:"); ImGui::NextColumn();
        if (clip.linked_toc_index >= 0) {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "%d", clip.linked_toc_index);
        } else {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "-");
        }
        ImGui::NextColumn();

        ImGui::Columns(1);
        ImGui::Separator();

        // Keyframe data
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Extracted Tracks (%d):", (int)clip.tracks.size());

        if (clip.tracks.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "No keyframe data extracted.");
        } else {
            // Summary
            int total_kf = 0;
            int valid_kf = 0;
            for (const auto& track : clip.tracks) {
                for (const auto& kf : track.keyframes) {
                    total_kf++;
                    if (kf.rotation.IsValid()) valid_kf++;
                }
            }
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f),
                "Total: %d keyframes, %d valid quaternions (%.1f%%)",
                total_kf, valid_kf, total_kf > 0 ? (100.0f * valid_kf / total_kf) : 0.0f);

            ImGui::Separator();

            // Per-track tables
            for (size_t t = 0; t < clip.tracks.size(); t++) {
                const BoneTrack& track = clip.tracks[t];

                char trackLabel[64];
                snprintf(trackLabel, sizeof(trackLabel), "Track/Bone %d (%d keyframes)###track%d",
                    track.bone_index, (int)track.keyframes.size(), (int)t);

                if (ImGui::CollapsingHeader(trackLabel, t == 0 ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {

                    if (ImGui::BeginTable(("KFTable" + std::to_string(t)).c_str(), 6,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                        ImVec2(0, 150))) {

                        ImGui::TableSetupScrollFreeze(0, 1);
                        ImGui::TableSetupColumn("KF#", ImGuiTableColumnFlags_WidthFixed, 40);
                        ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 60);
                        ImGui::TableSetupColumn("W", ImGuiTableColumnFlags_WidthFixed, 70);
                        ImGui::TableSetupColumn("X", ImGuiTableColumnFlags_WidthFixed, 70);
                        ImGui::TableSetupColumn("Y", ImGuiTableColumnFlags_WidthFixed, 70);
                        ImGui::TableSetupColumn("Z", ImGuiTableColumnFlags_WidthFixed, 70);
                        ImGui::TableHeadersRow();

                        for (size_t k = 0; k < track.keyframes.size(); k++) {
                            const Keyframe& kf = track.keyframes[k];

                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::Text("%d", (int)k);

                            ImGui::TableSetColumnIndex(1);
                            ImGui::Text("%.2f", kf.time);

                            ImVec4 quatColor = kf.rotation.IsValid() ?
                                ImVec4(0.5f, 1.0f, 0.5f, 1.0f) : ImVec4(1.0f, 0.5f, 0.5f, 1.0f);

                            ImGui::TableSetColumnIndex(2);
                            ImGui::TextColored(quatColor, "%.4f", kf.rotation.w);
                            ImGui::TableSetColumnIndex(3);
                            ImGui::TextColored(quatColor, "%.4f", kf.rotation.x);
                            ImGui::TableSetColumnIndex(4);
                            ImGui::TextColored(quatColor, "%.4f", kf.rotation.y);
                            ImGui::TableSetColumnIndex(5);
                            ImGui::TextColored(quatColor, "%.4f", kf.rotation.z);
                        }
                        ImGui::EndTable();
                    }
                }
            }
        }
    }

    void RenderExportView() {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Export Animation Data");
        ImGui::Separator();

        ImGui::Text("Output Path:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(400);
        ImGui::InputText("##exportPath", exportPath, IM_ARRAYSIZE(exportPath));

        ImGui::Separator();

        // Single clip export
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "Export Single Clip:");
        ImGui::Text("Selected Clip: %d", selectedClip);
        ImGui::SameLine();
        if (ImGui::Button("Export Selected Clip to JSON")) {
            if (selectedClip >= 0 && selectedClip < (int)parser.clips.size()) {
                if (parser.ExportClipToJson(selectedClip, exportPath)) {
                    Log("Exported clip " + std::to_string(selectedClip) + " to " + exportPath);
                } else {
                    Log("Failed to export clip.");
                }
            } else {
                Log("No clip selected.");
            }
        }

        ImGui::Separator();

        // Batch export
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "Batch Export:");
        static int batchCount = 100;
        ImGui::Text("Export first N clips:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        ImGui::InputInt("##batchCount", &batchCount);
        if (batchCount < 1) batchCount = 1;
        if (batchCount > (int)parser.clips.size()) batchCount = (int)parser.clips.size();

        ImGui::SameLine();
        if (ImGui::Button("Export Batch to JSON")) {
            std::string batchPath = std::string(exportPath);
            if (batchPath.find(".json") == std::string::npos) {
                batchPath += "_batch.json";
            }
            if (parser.ExportAllClipsToJson(batchPath, batchCount)) {
                Log("Exported " + std::to_string(batchCount) + " clips to " + batchPath);
            } else {
                Log("Failed to export batch.");
            }
        }

        ImGui::Separator();

        // Export all
        if (ImGui::Button("Export ALL Clips (may take a while)")) {
            std::string allPath = std::string(exportPath);
            if (allPath.find(".json") == std::string::npos) {
                allPath += "_all.json";
            }
            if (parser.ExportAllClipsToJson(allPath, -1)) {
                Log("Exported all " + std::to_string(parser.clips.size()) + " clips to " + allPath);
            } else {
                Log("Failed to export all clips.");
            }
        }

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
            "Note: Exported JSON contains quaternion keyframes [w,x,y,z] for each bone/track.");
    }
};

// =========================================================
// MAIN ENTRY POINT
// =========================================================

static void glfw_error_callback(int error, const char* description) {
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

int main(int, char**) {
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) return 1;

    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow* window = glfwCreateWindow(1500, 950, "Fable 2 Animation Viewer", nullptr, nullptr);
    if (window == nullptr) return 1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    FableAnimApp app;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        app.Render();

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}