#pragma once

#include "QuestGraph.h"

namespace Quest {

Graph BuildStoryGraph(const std::string& title,
                      const std::string& decompiled_lua,
                      const ReferenceCatalog& references);

}
