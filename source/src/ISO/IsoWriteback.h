#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace ISO {
namespace Writeback {

using Progress = std::function<void(std::size_t, std::size_t,
                                    const std::string&)>;

bool IsSession();
std::filesystem::path BackupDirectory();
std::filesystem::path ManifestPath();
bool BackupExists();

bool CreateBackup(const std::vector<std::string>& members,
                  const Progress& progress, std::string& error);
bool EnsureBackedUp(const std::vector<std::string>& paths,
                    std::string& error);
bool RestoreBackup(const Progress& progress, std::string& error);

bool WriteMember(const std::string& path,
                 const std::vector<uint8_t>& bytes,
                 bool require_backup, std::string& error);

}
}
