#pragma once

#include "QuestGraph.h"

#include <string>
#include <vector>

namespace Quest {





std::vector<QuestRewardReference> ExtractQuestRecordRewards(
    const std::string& globals_gdb_path,
    const std::string& decompiled_lua);

}
