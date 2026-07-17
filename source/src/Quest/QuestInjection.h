#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace QuestInjection {

struct BankTarget {
    std::string path;
    int gameflow_lua_index = -1;
    int gameflow_text_index = -1;
    int quest_script_index = -1;
    std::vector<uint8_t> gameflow_source;
};



bool Inject(const std::string& root_dir,
            const std::string& quest_id,
            const std::string& quest_lua,
            const std::string& eligibility_lua,
            const std::vector<std::pair<std::string, std::string>>&
                localized_text,
            const std::vector<BankTarget>& targets,
            std::string& result,
            std::string& error);

}
