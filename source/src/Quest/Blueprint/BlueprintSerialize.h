#pragma once

#include <string>

#include "BlueprintGraph.h"

namespace Quest {
namespace Bp {




std::string SerializeToString(const BlueprintQuest& quest);
bool DeserializeFromString(const std::string& text, BlueprintQuest& out,
                           std::string& error);

bool SaveToFile(const BlueprintQuest& quest, const std::string& path,
                std::string& error);
bool LoadFromFile(const std::string& path, BlueprintQuest& out,
                  std::string& error);

}
}
