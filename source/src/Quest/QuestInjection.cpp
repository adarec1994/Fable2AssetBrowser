#include "QuestInjection.h"

#include "QuestAuthoring.h"
#include "../BNKCore.cpp"
#include "../Level/IO/BnkWriter.h"
#include "../Level/Database/TextBank.h"
#include "../ISO/IsoMount.h"
#include "../ISO/IsoWriteback.h"
#include "../Utilities/DebugLog.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace QuestInjection {
namespace {

bool read_file(const std::string& path, std::vector<uint8_t>& bytes,
               std::string& error) {
    if (ISO::IsoMount::is_iso_path(path)) {
        const std::string member = ISO::IsoMount::strip_iso_prefix(path);
        const ISO::MountedFile* file =
            ISO::IsoMount::instance().find(member);
        if (!file) {
            error = "could not find " + path;
            return false;
        }
        bytes = ISO::IsoMount::instance().read_file(member);
        if (bytes.size() != file->size) {
            error = "short read of " + path;
            return false;
        }
        return true;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "could not read " + path;
        return false;
    }
    input.seekg(0, std::ios::end);
    const std::streamoff length = input.tellg();
    if (length < 0) {
        error = "could not measure " + path;
        return false;
    }
    input.seekg(0, std::ios::beg);
    bytes.resize(static_cast<std::size_t>(length));
    input.read(reinterpret_cast<char*>(bytes.data()), length);
    if (!input) {
        error = "short read of " + path;
        return false;
    }
    return true;
}

bool write_file_atomically(const std::string& path,
                           const std::vector<uint8_t>& bytes,
                           std::string& error) {
    if (ISO::IsoMount::is_iso_path(path)) {
        return ISO::Writeback::WriteMember(path, bytes, false, error);
    }
    namespace fs = std::filesystem;
    const std::string temporary = path + ".quest_restore_tmp";
    {
        std::ofstream output(temporary,
                             std::ios::binary | std::ios::trunc);
        if (!output) {
            error = "could not create rollback file for " + path;
            return false;
        }
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        if (!output) {
            error = "could not write rollback file for " + path;
            return false;
        }
    }
    std::error_code ec;
    fs::remove(path, ec);
    if (ec) {
        fs::remove(temporary, ec);
        error = "could not replace " + path + " during rollback";
        return false;
    }
    fs::rename(temporary, path, ec);
    if (ec) {
        error = "could not restore " + path + ": " + ec.message();
        return false;
    }
    return true;
}

std::vector<uint8_t> to_bytes(const std::string& text) {
    return std::vector<uint8_t>(text.begin(), text.end());
}

std::string quest_entry_name(const std::string& quest_id) {
    std::string lower = quest_id;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    return "scripts\\quests\\" + lower + ".lua";
}

}

bool Inject(const std::string& root_dir,
            const std::string& quest_id,
            const std::string& quest_lua,
            const std::string& eligibility_lua,
            const std::vector<std::pair<std::string, std::string>>&
                localized_text,
            const std::vector<BankTarget>& targets,
            std::string& result,
            std::string& error) {
    DebugLog::Scope debug_scope("Inject quest", quest_id + " | banks=" +
        std::to_string(targets.size()));
    result.clear();
    error.clear();
    if (!Quest::IsValidQuestId(quest_id) || quest_lua.empty() ||
        eligibility_lua.empty()) {
        error = "the authored quest data is incomplete";
        return false;
    }
    if (targets.empty()) {
        error = "no gamescripts.bnk files were found";
        return false;
    }

    struct Snapshot {
        std::string path;
        std::vector<uint8_t> bytes;
    };

    
    
    
    for (const BankTarget& target : targets) {
        BnkCache::invalidate(target.path);
    }

    std::vector<Snapshot> snapshots;
    snapshots.reserve(targets.size());
    for (const BankTarget& target : targets) {
        if (target.gameflow_lua_index < 0 ||
            target.gameflow_text_index < 0 ||
            target.gameflow_source.empty()) {
            error = "gameflow.lua or gameflow.txt is missing from " +
                    target.path;
            return false;
        }
        Snapshot snapshot;
        snapshot.path = target.path;
        if (!read_file(target.path, snapshot.bytes, error)) {
            return false;
        }
        snapshots.push_back(std::move(snapshot));
    }

    std::unordered_map<uint32_t, std::string> text_edits;
    for (const auto& entry : localized_text) {
        if (entry.first.empty()) continue;
        text_edits[TextBank::TagHash(entry.first)] = entry.second;
    }
    if (text_edits.count(TextBank::TagHash("Quest_" + quest_id)) == 0) {
        text_edits.emplace(TextBank::TagHash("Quest_" + quest_id),
                           quest_id);
    }
    if (!TextBank::ApplyEdits(root_dir, text_edits, error)) {
        error = "could not register the quest title: " + error;
        return false;
    }

    std::size_t written = 0;
    for (const BankTarget& target : targets) {
        const std::string gameflow_source(
            target.gameflow_source.begin(), target.gameflow_source.end());
        std::string patched_gameflow;
        if (!Quest::PatchGameflowEligibility(
                gameflow_source, quest_id, eligibility_lua,
                patched_gameflow, error)) {
            error = std::filesystem::path(target.path).filename().string() +
                    ": " + error;
            break;
        }

        std::vector<BnkWriter::EntryReplacement> replacements;
        replacements.push_back(
            {target.gameflow_lua_index, to_bytes(patched_gameflow)});
        replacements.push_back(
            {target.gameflow_text_index, to_bytes(patched_gameflow)});
        std::vector<BnkWriter::EntryAddition> additions;
        if (target.quest_script_index >= 0) {
            replacements.push_back(
                {target.quest_script_index, to_bytes(quest_lua)});
        } else {
            additions.push_back({quest_entry_name(quest_id),
                                 to_bytes(quest_lua)});
        }

        if (!BnkWriter::RebuildWithChanges(target.path, replacements,
                                           additions, error)) {
            error = std::filesystem::path(target.path).filename().string() +
                    ": " + error;
            break;
        }
        ++written;
    }

    if (written != targets.size()) {
        std::string rollback_error;
        for (const Snapshot& snapshot : snapshots) {
            std::string one_error;
            if (!write_file_atomically(snapshot.path, snapshot.bytes,
                                       one_error)) {
                if (!rollback_error.empty()) rollback_error += "; ";
                rollback_error += one_error;
            }
        }
        if (!rollback_error.empty()) {
            error += " (rollback also failed: " + rollback_error + ')';
        }
        return false;
    }

    std::ostringstream message;
    message << "Saved " << quest_id << " to " << targets.size()
            << " script bank" << (targets.size() == 1 ? "" : "s") << ".";
    result = message.str();
    debug_scope.Result("success");
    return true;
}

}
