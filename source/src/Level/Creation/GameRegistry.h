#pragma once

#include <string>
#include <vector>

namespace Level {
namespace Creation {


bool RegisterInScenariosList(const std::string& data_dir,
                             const std::string& region,
                             std::string&       error);



bool RegisterInDirManifest(const std::string&              data_dir,
                           const std::vector<std::string>& virtual_paths,
                           std::string&                    error);



bool UnregisterLevel(const std::string& data_dir,
                     const std::string& region,
                     std::string&       error);



std::vector<std::string> ListCustomLevels(const std::string& data_dir);




bool SyncDebugMenuCustomLevels(const std::string& data_dir,
                               std::string&       error);

}
}
