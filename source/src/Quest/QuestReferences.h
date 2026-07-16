#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Quest {

struct CutsceneDialogueLine {
    std::string text_tag;
    std::string speaker;
};

enum class CutsceneTimelineKind {
    Dialogue,
    ActorAction,
};




struct CutsceneTimelineEntry {
    CutsceneTimelineKind kind = CutsceneTimelineKind::ActorAction;
    std::string text_tag;
    std::string speaker;
    std::string description;
    std::vector<std::string> details;
    std::vector<std::string> metadata;
};

struct CutsceneReference {
    std::vector<CutsceneDialogueLine> dialogue_lines;
    std::vector<std::string> dialogue_tags;
    std::vector<std::string> speakers;
    std::vector<CutsceneTimelineEntry> timeline;
};

std::vector<std::string> FindCutsceneIds(const std::string& decompiled_lua);

std::unordered_map<std::string, CutsceneReference>
ExtractCutsceneReferences(const std::vector<uint8_t>& interactive_cutscenes_gdb,
                          const std::vector<std::string>& cutscene_ids);

}
