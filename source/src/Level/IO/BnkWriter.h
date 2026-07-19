#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace BnkWriter {

struct EntryReplacement {
    int file_index = -1;
    std::vector<uint8_t> payload;
};

struct EntryAddition {
    std::string name;
    std::vector<uint8_t> payload;
};

bool AddEntriesToBnkBytes(std::vector<uint8_t>& bnk,
                          const std::vector<EntryAddition>& adds,
                          std::string& err);



bool RebuildWithChanges(const std::string& bnk_path,
                        const std::vector<EntryReplacement>& repls,
                        const std::vector<EntryAddition>& adds,
                        std::string& err);

// Builds the rebuilt bank into staged_path and leaves bnk_path untouched,
// so multi-bank operations can stage everything first and then commit via
// cheap renames instead of snapshotting whole banks for rollback.
// Not available for ISO-mounted paths.
bool RebuildWithChangesStaged(const std::string& bnk_path,
                              const std::vector<EntryReplacement>& repls,
                              const std::vector<EntryAddition>& adds,
                              const std::string& staged_path,
                              std::string& err);

bool RebuildWithReplacedEntries(const std::string& bnk_path,
                                const std::vector<EntryReplacement>& repls,
                                std::string& err);

bool RebuildWithReplacedEntry(const std::string& bnk_path,
                              int file_index,
                              const std::vector<uint8_t>& new_payload,
                              std::string& err);

bool RebuildIsoLevelBnk(const std::string& virtual_path,
                        int file_index,
                        const std::vector<uint8_t>& new_payload,
                        std::string& err);

}
