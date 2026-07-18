#include "IsoWriteback.h"

#include "IsoMount.h"
#include "../Utilities/DebugLog.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <limits>
#include <mutex>
#include <set>

namespace ISO {
namespace Writeback {
namespace {

constexpr uint64_t kSectorSize = 2048;

std::mutex& backup_mutex() {
    static std::mutex value;
    return value;
}

std::mutex& mutation_mutex() {
    static std::mutex value;
    return value;
}

bool normalize_member(const std::string& input, std::string& output) {
    std::string value = IsoMount::strip_iso_prefix(input);
    std::replace(value.begin(), value.end(), '\\', '/');
    const std::filesystem::path path(value);
    const std::filesystem::path normalized = path.lexically_normal();
    if (normalized.empty() || normalized.is_absolute() ||
        normalized.has_root_name() || normalized.has_root_directory()) {
        return false;
    }
    for (const auto& part : normalized) {
        if (part == ".." || part == ".") return false;
    }
    output = normalized.generic_string();
    return !output.empty();
}

bool install_temporary(const std::filesystem::path& temporary,
                       const std::filesystem::path& path,
                       std::string& error) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
    ec.clear();
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        error = "Could not install " + path.string() + ": " + ec.message();
        return false;
    }
    return true;
}

bool load_manifest(std::vector<std::string>& members,
                   std::string& error) {
    members.clear();
    std::ifstream input(ManifestPath());
    if (!input) {
        error = "The ISO backup manifest is missing.";
        return false;
    }
    std::set<std::string> unique;
    std::string line;
    while (std::getline(input, line)) {
        while (!line.empty() &&
               (line.back() == '\r' || line.back() == '\n')) {
            line.pop_back();
        }
        std::string member;
        if (!line.empty() && normalize_member(line, member) &&
            unique.insert(member).second) {
            members.push_back(std::move(member));
        }
    }
    if (members.empty()) {
        error = "The ISO backup manifest is empty.";
        return false;
    }
    return true;
}

bool save_manifest(const std::vector<std::string>& members,
                   std::string& error) {
    const std::filesystem::path path = ManifestPath();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        error = "Could not create the ISO backup folder: " + ec.message();
        return false;
    }
    const std::filesystem::path temporary =
        std::filesystem::path(path.string() + ".tmp_write");
    {
        std::ofstream output(temporary, std::ios::trunc);
        if (!output) {
            error = "Could not create the ISO backup manifest.";
            return false;
        }
        for (const std::string& member : members) output << member << '\n';
        if (!output) {
            error = "Could not write the ISO backup manifest.";
            return false;
        }
    }
    std::filesystem::remove(path, ec);
    ec.clear();
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        error = "Could not install the ISO backup manifest: " +
                ec.message();
        return false;
    }
    return true;
}

bool extract_member(const std::string& member, std::string& error) {
    IsoMount& iso = IsoMount::instance();
    const MountedFile* file = iso.find(member);
    if (!file) {
        error = "The ISO does not contain " + member;
        return false;
    }
    const uint64_t size = file->size;
    const uint64_t source = iso.abs_offset_of(member);
    const std::filesystem::path path =
        BackupDirectory() / std::filesystem::path(member);
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        error = "Could not create " + path.parent_path().string() + ": " +
                ec.message();
        return false;
    }
    const std::filesystem::path temporary =
        std::filesystem::path(path.string() + ".tmp_write");
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "Could not create " + temporary.string();
        return false;
    }
    constexpr std::size_t chunk_size = 8u << 20;
    std::vector<uint8_t> buffer;
    buffer.resize(static_cast<std::size_t>(
        std::min<uint64_t>(chunk_size, size)));
    uint64_t offset = 0;
    while (offset < size) {
        const std::size_t count = static_cast<std::size_t>(
            std::min<uint64_t>(chunk_size, size - offset));
        if (buffer.size() != count) buffer.resize(count);
        if (!iso.raw_read_abs(source + offset, buffer.data(), count)) {
            error = "Could not extract " + member + " from the ISO.";
            return false;
        }
        output.write(reinterpret_cast<const char*>(buffer.data()),
                     static_cast<std::streamsize>(count));
        if (!output) {
            error = "Short write to " + temporary.string();
            return false;
        }
        offset += count;
    }
    output.close();
    return install_temporary(temporary, path, error);
}

bool verify_absolute(IsoMount& iso, uint64_t absolute,
                     const std::vector<uint8_t>& bytes) {
    constexpr std::size_t chunk_size = 1u << 20;
    std::vector<uint8_t> check;
    check.resize(std::min(chunk_size, bytes.size()));
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const std::size_t count =
            std::min(chunk_size, bytes.size() - offset);
        if (check.size() != count) check.resize(count);
        if (!iso.raw_read_abs(absolute + offset, check.data(), count) ||
            !std::equal(check.begin(), check.end(),
                        bytes.begin() + offset)) {
            return false;
        }
        offset += count;
    }
    return true;
}

bool verify_member(IsoMount& iso, const std::string& member,
                   const std::vector<uint8_t>& bytes) {
    const MountedFile* file = iso.find(member);
    if (!file || file->size != bytes.size()) return false;
    if (bytes.empty()) return true;
    return verify_absolute(iso, iso.abs_offset_of(member), bytes);
}

bool copy_file_to_absolute(IsoMount& iso,
                           const std::filesystem::path& source,
                           uint64_t absolute, uint64_t size,
                           std::string& error) {
    std::ifstream input(source, std::ios::binary);
    if (!input) {
        error = "Could not read " + source.string();
        return false;
    }
    constexpr std::size_t chunk_size = 8u << 20;
    std::vector<uint8_t> buffer;
    std::vector<uint8_t> check;
    buffer.resize(static_cast<std::size_t>(
        std::min<uint64_t>(chunk_size, size)));
    uint64_t offset = 0;
    while (offset < size) {
        const std::size_t count = static_cast<std::size_t>(
            std::min<uint64_t>(chunk_size, size - offset));
        if (buffer.size() != count) buffer.resize(count);
        input.read(reinterpret_cast<char*>(buffer.data()),
                   static_cast<std::streamsize>(count));
        if (input.gcount() != static_cast<std::streamsize>(count)) {
            error = "Short read of " + source.string();
            return false;
        }
        if (!iso.raw_write_abs(absolute + offset, buffer.data(), count)) {
            error = "Could not write restored ISO data.";
            return false;
        }
        check.resize(count);
        if (!iso.raw_read_abs(absolute + offset, check.data(), count) ||
            check != buffer) {
            error = "ISO restore verification failed.";
            return false;
        }
        offset += count;
    }
    return true;
}

bool restore_member_from_file(const std::string& member,
                              const std::filesystem::path& source,
                              std::string& error) {
    std::error_code ec;
    const uint64_t size = std::filesystem::file_size(source, ec);
    if (ec || size > std::numeric_limits<uint32_t>::max()) {
        error = "Invalid ISO backup file: " + source.string();
        return false;
    }
    std::lock_guard<std::mutex> lock(mutation_mutex());
    IsoMount& iso = IsoMount::instance();
    const MountedFile* current = iso.find(member);
    if (!current) {
        error = "The ISO does not contain " + member;
        return false;
    }
    const uint32_t old_sector = current->sector;
    const uint32_t old_size = current->size;
    uint32_t sector = old_sector;
    uint64_t absolute = iso.abs_offset_of(member);
    if (size > current->size) {
        const uint64_t image_size = iso.iso_size();
        if (image_size < iso.base_offset()) {
            error = "The mounted ISO size is invalid.";
            return false;
        }
        const uint64_t relative =
            ((image_size - iso.base_offset() + kSectorSize - 1) /
             kSectorSize) * kSectorSize;
        const uint64_t sector64 = relative / kSectorSize;
        if (sector64 > std::numeric_limits<uint32_t>::max()) {
            error = "The ISO has no addressable space for the backup.";
            return false;
        }
        sector = static_cast<uint32_t>(sector64);
        absolute = iso.base_offset() + relative;
    }
    if (size > 0 &&
        !copy_file_to_absolute(iso, source, absolute, size, error)) {
        return false;
    }
    if (!iso.repoint(member, sector, static_cast<uint32_t>(size))) {
        error = "Could not restore the ISO directory entry for " + member;
        return false;
    }
    const MountedFile* restored = iso.find(member);
    if (!restored || restored->sector != sector || restored->size != size) {
        iso.repoint(member, old_sector, old_size);
        error = "ISO restore directory verification failed for " + member;
        return false;
    }
    return true;
}

}

bool IsSession() {
    IsoMount& iso = IsoMount::instance();
    return iso.is_mounted() && !iso.iso_path().empty();
}

std::filesystem::path BackupDirectory() {
    if (!IsSession()) return {};
    return std::filesystem::path(IsoMount::instance().iso_path() +
                                 ".f2ab_backup");
}

std::filesystem::path ManifestPath() {
    const std::filesystem::path directory = BackupDirectory();
    return directory.empty() ? directory : directory / "backup.manifest";
}

bool BackupExists() {
    const std::filesystem::path manifest = ManifestPath();
    if (manifest.empty()) return false;
    std::error_code ec;
    return std::filesystem::is_regular_file(manifest, ec);
}

bool CreateBackup(const std::vector<std::string>& members,
                  const Progress& progress, std::string& error) {
    DebugLog::Scope debug_scope("Create ISO backup",
        "requested=" + std::to_string(members.size()));
    std::lock_guard<std::mutex> lock(backup_mutex());
    if (!IsSession()) {
        error = "No ISO is mounted.";
        return false;
    }
    std::vector<std::string> normalized;
    std::set<std::string> unique;
    for (const std::string& value : members) {
        std::string member;
        if (!normalize_member(value, member)) continue;
        if (!IsoMount::instance().find(member)) continue;
        if (unique.insert(member).second) normalized.push_back(member);
    }
    if (normalized.empty()) {
        error = "No editable files were found in the ISO.";
        return false;
    }
    for (std::size_t index = 0; index < normalized.size(); ++index) {
        if (progress) progress(index, normalized.size(), normalized[index]);
        if (!extract_member(normalized[index], error)) return false;
    }
    if (!save_manifest(normalized, error)) return false;
    if (progress) {
        progress(normalized.size(), normalized.size(), std::string());
    }
    debug_scope.Result("success | files=" +
                       std::to_string(normalized.size()));
    return true;
}

bool EnsureBackedUp(const std::vector<std::string>& paths,
                    std::string& error) {
    std::lock_guard<std::mutex> lock(backup_mutex());
    if (!BackupExists()) {
        error = "Editing is disabled until you create an ISO file backup.";
        return false;
    }
    std::vector<std::string> manifest;
    if (!load_manifest(manifest, error)) return false;
    std::set<std::string> covered(manifest.begin(), manifest.end());
    bool changed = false;
    for (const std::string& value : paths) {
        std::string member;
        if (!normalize_member(value, member)) {
            error = "Invalid ISO member path: " + value;
            return false;
        }
        const std::filesystem::path saved =
            BackupDirectory() / std::filesystem::path(member);
        std::error_code ec;
        if (covered.count(member) &&
            std::filesystem::is_regular_file(saved, ec)) {
            continue;
        }
        if (!extract_member(member, error)) return false;
        if (covered.insert(member).second) manifest.push_back(member);
        changed = true;
    }
    return !changed || save_manifest(manifest, error);
}

bool RestoreBackup(const Progress& progress, std::string& error) {
    DebugLog::Scope debug_scope("Restore ISO backup");
    std::vector<std::string> members;
    {
        std::lock_guard<std::mutex> lock(backup_mutex());
        if (!load_manifest(members, error)) return false;
    }
    for (std::size_t index = 0; index < members.size(); ++index) {
        if (progress) progress(index, members.size(), members[index]);
        if (!restore_member_from_file(
                members[index],
                BackupDirectory() /
                    std::filesystem::path(members[index]),
                error)) return false;
    }
    if (progress) progress(members.size(), members.size(), std::string());
    debug_scope.Result("success | files=" +
                       std::to_string(members.size()));
    return true;
}

bool WriteMember(const std::string& path,
                 const std::vector<uint8_t>& bytes,
                 bool require_backup, std::string& error) {
    DebugLog::Scope debug_scope("Write ISO member", path + " | bytes=" +
        std::to_string(bytes.size()) + " | backup=" +
        (require_backup ? "required" : "not-required"));
    std::string member;
    if (!normalize_member(path, member)) {
        error = "Invalid ISO member path: " + path;
        return false;
    }
    if (bytes.size() > std::numeric_limits<uint32_t>::max()) {
        error = "The replacement is too large for an XDVDFS file entry.";
        return false;
    }
    if (require_backup && !EnsureBackedUp({member}, error)) return false;

    std::lock_guard<std::mutex> lock(mutation_mutex());
    IsoMount& iso = IsoMount::instance();
    const MountedFile* current = iso.find(member);
    if (!current) {
        error = "The ISO does not contain " + member;
        return false;
    }
    const uint32_t old_sector = current->sector;
    const uint32_t old_size = current->size;
    if (!require_backup && bytes.size() <= old_size) {
        if (!bytes.empty() &&
            !iso.write_at(member, 0, bytes.data(), bytes.size())) {
            error = "Could not write " + member + " inside the ISO.";
            return false;
        }
        if (!iso.repoint(member, old_sector,
                         static_cast<uint32_t>(bytes.size()))) {
            error = "Could not update the ISO directory entry for " +
                    member;
            return false;
        }
        if (!verify_member(iso, member, bytes)) {
            error = "ISO verification failed for " + member;
            return false;
        }
        debug_scope.Result("success | in-place");
        return true;
    }

    if (bytes.empty()) {
        if (!iso.repoint(member, old_sector, 0)) {
            error = "Could not clear " + member + " inside the ISO.";
            return false;
        }
        debug_scope.Result("success | cleared");
        return true;
    }

    const uint64_t image_size = iso.iso_size();
    if (image_size < iso.base_offset()) {
        error = "The mounted ISO size is invalid.";
        return false;
    }
    const uint64_t relative =
        ((image_size - iso.base_offset() + kSectorSize - 1) /
         kSectorSize) * kSectorSize;
    const uint64_t sector64 = relative / kSectorSize;
    if (sector64 > std::numeric_limits<uint32_t>::max()) {
        error = "The ISO has no addressable space for the replacement.";
        return false;
    }
    const uint64_t absolute = iso.base_offset() + relative;
    if (!iso.raw_write_abs(absolute, bytes.data(), bytes.size())) {
        error = "Could not append " + member + " to the ISO.";
        return false;
    }
    if (!verify_absolute(iso, absolute, bytes)) {
        error = "ISO verification failed before replacing " + member;
        return false;
    }
    if (!iso.repoint(member, static_cast<uint32_t>(sector64),
                     static_cast<uint32_t>(bytes.size()))) {
        error = "Could not repoint the ISO directory entry for " + member;
        return false;
    }
    if (!verify_member(iso, member, bytes)) {
        iso.repoint(member, old_sector, old_size);
        error = "ISO verification failed after replacing " + member;
        return false;
    }
    debug_scope.Result("success | appended");
    return true;
}

}
}
