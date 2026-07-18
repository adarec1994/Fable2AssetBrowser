#include "NewLevel.h"
#include "GameRegistry.h"
#include "LandscapeAuthoring.h"

#include "BNKCore.cpp"
#include "GDB/GdbEdit.h"
#include "GDB/GdbParser.h"
#include "UI/OutputLog.h"
#include "Utilities/GameBackup.h"
#include "Utilities/State.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <zlib.h>

namespace Level {
namespace Creation {

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return (char)std::tolower(c); });
    return s;
}

std::string backslash(std::string s) {
    std::replace(s.begin(), s.end(), '/', '\\');
    return s;
}

void be_u32(std::string& out, uint32_t v) {
    out.push_back(char((v >> 24) & 0xFF));
    out.push_back(char((v >> 16) & 0xFF));
    out.push_back(char((v >> 8) & 0xFF));
    out.push_back(char(v & 0xFF));
}

void be_f32(std::string& out, float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    be_u32(out, bits);
}

void set_be_f32(std::vector<uint8_t>& bytes, uint32_t offset, float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    bytes[offset + 0] = uint8_t((bits >> 24) & 0xFF);
    bytes[offset + 1] = uint8_t((bits >> 16) & 0xFF);
    bytes[offset + 2] = uint8_t((bits >> 8) & 0xFF);
    bytes[offset + 3] = uint8_t(bits & 0xFF);
}

bool gzip_compress(const std::string& raw, const std::string& embedded_name,
                   std::vector<uint8_t>& out, std::string& error) {
    z_stream stream;
    std::memset(&stream, 0, sizeof(stream));
    if (deflateInit2(&stream, Z_BEST_COMPRESSION, Z_DEFLATED, 15 + 16, 8,
                     Z_DEFAULT_STRATEGY) != Z_OK) {
        error = "could not create blank terrain compressor";
        return false;
    }
    gz_header header;
    std::memset(&header, 0, sizeof(header));
    std::string name = embedded_name;
    header.name = reinterpret_cast<Bytef*>(name.data());
    header.os = 0;
    deflateSetHeader(&stream, &header);
    out.resize(deflateBound(&stream, (uLong)raw.size()) + name.size() + 64);
    stream.next_in = reinterpret_cast<Bytef*>(
        const_cast<char*>(raw.data()));
    stream.avail_in = (uInt)raw.size();
    stream.next_out = out.data();
    stream.avail_out = (uInt)out.size();
    const int result = deflate(&stream, Z_FINISH);
    if (result != Z_STREAM_END) {
        deflateEnd(&stream);
        error = "could not compress blank terrain";
        return false;
    }
    out.resize(out.size() - stream.avail_out);
    deflateEnd(&stream);
    return true;
}

bool build_blank_heightfield(const std::string& region,
                             const std::string& hfid,
                             std::vector<uint8_t>& out,
                             std::string& error) {
    constexpr int width = 2;
    constexpr int height = 2;
    static const uint8_t cell_tail[10] = {
        0x00, 0x00, 0x00, 0x00, 0xCA, 0xD8, 0x17, 0x57, 0x00, 0x00};
    std::string raw;
    raw.reserve(20 + size_t(width) * size_t(height) * 14);
    be_f32(raw, 0.5f);
    be_u32(raw, 0);
    be_u32(raw, 0);
    be_u32(raw, width);
    be_u32(raw, height);
    for (int i = 0; i < width * height; ++i) {
        be_f32(raw, 0.0f);
        raw.append(reinterpret_cast<const char*>(cell_tail),
                   sizeof(cell_tail));
    }
    return gzip_compress(raw,
                         "export_xbox360\\worlds\\albion\\" + region +
                             "\\" + hfid + ".ghf",
                         out, error);
}


std::string replace_ci(const std::string& hay, const std::string& from,
                       const std::string& to) {
    const std::string hay_low = lower(hay);
    const std::string from_low = lower(from);
    std::string out;
    out.reserve(hay.size());
    size_t pos = 0;
    for (;;) {
        const size_t hit = hay_low.find(from_low, pos);
        if (hit == std::string::npos) {
            out.append(hay, pos, hay.size() - pos);
            return out;
        }
        out.append(hay, pos, hit - pos);
        out.append(to);
        pos = hit + from_low.size();
    }
}

struct DonorFile {
    std::string          rel_path;   
    std::vector<uint8_t> bytes;
};

struct SpawnPointMapping {
    std::string name;
    uint32_t record_hash = 0;
};

bool parse_hex_hash(const std::string& text, uint32_t& value) {
    if (text.empty() || text.size() > 8) return false;
    uint32_t parsed = 0;
    for (const char c : text) {
        parsed <<= 4;
        if (c >= '0' && c <= '9') parsed |= uint32_t(c - '0');
        else if (c >= 'A' && c <= 'F') parsed |= uint32_t(c - 'A' + 10);
        else if (c >= 'a' && c <= 'f') parsed |= uint32_t(c - 'a' + 10);
        else return false;
    }
    value = parsed;
    return true;
}

bool build_custom_spawn_save(const std::vector<uint8_t>& source,
                             const std::string& level_name,
                             std::vector<uint8_t>& out,
                             std::vector<SpawnPointMapping>& mappings,
                             std::string& error) {
    std::string xml(source.begin(), source.end());
    const std::string entity_open = "<Entity name=\"";
    const std::string entity_close = "</Entity>";
    std::string clean;
    clean.reserve(xml.size());
    bool found_start = false;
    bool found_teleport = false;
    size_t pos = 0;
    size_t copied = 0;
    while ((pos = xml.find(entity_open, pos)) != std::string::npos) {
        const size_t name_start = pos + entity_open.size();
        const size_t name_end = xml.find('"', name_start);
        if (name_end == std::string::npos) break;
        const size_t close = xml.find(entity_close, name_end);
        if (close == std::string::npos) break;
        const size_t entity_end = close + entity_close.size();
        const size_t hash_start = xml.find("0x", name_end);
        if (hash_start == std::string::npos || hash_start > close) {
            clean.append(xml, copied, pos - copied);
            copied = entity_end;
            pos = entity_end;
            continue;
        }
        const size_t hash_end = xml.find_first_not_of(
            "0123456789abcdefABCDEF", hash_start + 2);
        if (hash_end == std::string::npos || hash_end > close) {
            clean.append(xml, copied, pos - copied);
            copied = entity_end;
            pos = entity_end;
            continue;
        }
        const std::string old_name =
            xml.substr(name_start, name_end - name_start);
        const std::string old_name_low = lower(old_name);
        bool is_start = old_name_low.rfind("startfrom", 0) == 0;
        bool is_teleport = old_name_low.rfind("teleportto", 0) == 0;
        const bool keep = (is_start && !found_start) ||
                          (is_teleport && !found_teleport);
        clean.append(xml, copied, pos - copied);
        if (keep) {
            uint32_t record_hash = 0;
            if (!parse_hex_hash(
                    xml.substr(hash_start + 2, hash_end - hash_start - 2),
                    record_hash)) {
                error = "Donor player-start save contains an invalid hash.";
                return false;
            }
            std::string new_name =
                std::string(is_start ? "StartFrom_" : "TeleportTo_") +
                level_name;
            clean.append(xml, pos, name_start - pos);
            clean += new_name;
            clean.append(xml, name_end, entity_end - name_end);
            mappings.push_back({new_name, record_hash});
            found_start = found_start || is_start;
            found_teleport = found_teleport || is_teleport;
        }
        copied = entity_end;
        pos = entity_end;
    }
    clean.append(xml, copied, xml.size() - copied);
    if (!found_start || !found_teleport) {
        error = "Donor level must contain both StartFrom and TeleportTo "
                "player-start entities.";
        return false;
    }
    out.assign(clean.begin(), clean.end());
    return true;
}

bool build_custom_spawn_gdb(
    const std::vector<uint8_t>& source,
    const std::vector<SpawnPointMapping>& mappings,
    std::vector<uint8_t>& out, std::string& error) {
    GdbEdit::GdbFile gdb;
    if (!gdb.Parse(source, error)) {
        error = "Donor player-start GDB could not be parsed: " + error;
        return false;
    }
    constexpr uint32_t environment_theme_day_set = 0x0843AB41u;
    uint32_t environment_owner = 0;
    for (size_t i = 0; i < gdb.RecordCount(); ++i) {
        GdbEdit::Field field;
        if (gdb.FindLocalField(gdb.RecordAt(i).hash,
                               environment_theme_day_set, field)) {
            environment_owner = gdb.RecordAt(i).hash;
            break;
        }
    }
    if (environment_owner != 0) {
        std::vector<GdbEdit::Field> fields;
        gdb.Fields(gdb.FindRecord(environment_owner), fields);
        for (const GdbEdit::Field& field : fields) {
            if (field.hash != environment_theme_day_set) {
                gdb.RemoveField(environment_owner, field.hash);
            }
        }
        gdb.SetFieldValue(environment_owner, environment_theme_day_set, 0);
    }
    std::unordered_set<uint32_t> keep;
    std::vector<uint32_t> pending;
    if (environment_owner != 0) keep.insert(environment_owner);
    for (const SpawnPointMapping& mapping : mappings) {
        if (!gdb.RecordByHash(mapping.record_hash)) {
            error = "Donor player-start record is missing from the GDB.";
            return false;
        }
        if (keep.insert(mapping.record_hash).second) {
            pending.push_back(mapping.record_hash);
        }
    }
    while (!pending.empty()) {
        const uint32_t hash = pending.back();
        pending.pop_back();
        std::vector<GdbEdit::Field> fields;
        if (!gdb.Fields(gdb.FindRecord(hash), fields)) continue;
        for (const GdbEdit::Field& field : fields) {
            if (field.type != 6 && field.type != 7) continue;
            if (!gdb.RecordByHash(field.value)) continue;
            if (keep.insert(field.value).second) pending.push_back(field.value);
        }
    }
    std::vector<uint32_t> remove;
    remove.reserve(gdb.RecordCount());
    for (size_t i = 0; i < gdb.RecordCount(); ++i) {
        const uint32_t hash = gdb.RecordAt(i).hash;
        if (keep.find(hash) == keep.end()) remove.push_back(hash);
    }
    for (uint32_t hash : remove) gdb.RemoveRecord(hash);
    for (const SpawnPointMapping& mapping : mappings) {
        gdb.AddNameMapping(mapping.name, mapping.record_hash);
    }
    out = gdb.Serialize();
    for (const SpawnPointMapping& mapping : mappings) {
        Gdb::Placement placement;
        if (!Gdb::LookupPlacement(out, mapping.record_hash, mapping.name,
                                  placement)) {
            error = "Player start position could not be reset in the clean GDB.";
            return false;
        }
        const float clean_position[3] = {0.0f, 0.0f, 1.0f};
        for (int axis = 0; axis < 3; ++axis) {
            const uint32_t offset = placement.pos_value_off[axis];
            if (offset == 0 || size_t(offset) + 4 > out.size()) {
                error = "Player start position is outside the clean GDB.";
                return false;
            }
            set_be_f32(out, offset, clean_position[axis]);
        }
    }
    return true;
}

bool skip_authored_donor_file(const std::string& rel_path) {
    const std::string path = lower(backslash(rel_path));
    const size_t slash = path.find_last_of('\\');
    const std::string leaf = slash == std::string::npos
                                 ? path
                                 : path.substr(slash + 1);
    const size_t dot = leaf.find_last_of('.');
    const std::string ext = dot == std::string::npos
                                ? std::string()
                                : leaf.substr(dot);
    if (path.find("\\lightprobedata\\") != std::string::npos) return true;
    return ext == ".ehf" || ext == ".ama" || ext == ".amm" ||
           ext == ".amr" || ext == ".hdb" || ext == ".genv" ||
           ext == ".havok_scenario" ||
           ext == ".texture_atlas" || ext == ".aim" ||
           ext == ".fdl" || ext == ".pdl" || ext == ".ppd";
}

bool load_donor_files(const std::string& data_dir, const std::string& donor,
                      std::vector<DonorFile>& out, std::string& error) {
    const std::string prefix = "worlds\\albion\\" + lower(donor) + "\\";
    size_t sources = 0;
    for (const char* bank : {"levels.bnk", "streaming.bnk"}) {
        const std::filesystem::path bnk_path =
            std::filesystem::path(data_dir) / bank;
        std::error_code ec;
        if (!std::filesystem::is_regular_file(bnk_path, ec)) continue;
        ++sources;
        try {
            const BnkCache::Entry bnk = BnkCache::get(bnk_path.string());
            const auto& files = bnk.reader->list_files();
            for (size_t i = 0; i < files.size(); ++i) {
                const std::string name = lower(backslash(files[i].name));
                if (name.rfind(prefix, 0) != 0) continue;
                DonorFile df;
                df.rel_path = name.substr(prefix.size());
                df.bytes = BnkCache::extract_bytes(bnk_path.string(), (int)i);
                out.push_back(std::move(df));
            }
        } catch (const std::exception& ex) {
            error = std::string(bank) + ": " + ex.what();
            return false;
        }
    }
    if (sources == 0) {
        error = "levels.bnk / streaming.bnk not found in " + data_dir;
        return false;
    }
    if (out.empty()) {
        error = "donor region '" + donor + "' not found in the game banks";
        return false;
    }
    return true;
}

std::string donor_heightfield_id(const std::vector<DonorFile>& donor_files) {
    for (const DonorFile& df : donor_files) {
        if (df.rel_path.find('\\') != std::string::npos) continue;
        const size_t dot = df.rel_path.find_last_of('.');
        if (dot != std::string::npos && df.rel_path.substr(dot) == ".ghf") {
            return df.rel_path.substr(0, dot);
        }
    }
    return {};
}

std::string build_engine_level(const std::string& region,
                               const std::string& hfid) {
    const std::string base = "worlds\\albion\\" + region +
                             "\\defaultscenario\\" + hfid;
    std::string out;
    out += "LevelGraphicsFile";           
    be_u32(out, 12);                      
    be_u32(out, 2);                       
    be_u32(out, 5);                       
    out += base + ".water";
    out.push_back('\0');
    be_u32(out, 32);                      
    out += base + ".mist";
    out.push_back('\0');
    return out;
}

std::string build_list_file(const std::string& region,
                            const std::string& hfid) {
    std::ostringstream os;
    os << "Worlds\\Albion\\" << region << "\\" << hfid << ".ghf\r\n";
    os << "Worlds\\Albion\\" << region
       << "\\DefaultScenario\\DefaultScenario.gdb\r\n";
    os << "Worlds\\Albion\\" << region
       << "\\DefaultScenario\\DefaultScenario.save\r\n";
    os << "Worlds\\Albion\\" << region
       << "\\DefaultScenario\\pathdata\\Config.ai_config\r\n";
    for (const char* leaf :
         {"defaultscenario.engine_level", "defaultscenario_models.bnk",
           "defaultscenario_textures.bnk",
          "defaultscenario_texture_headers.bnk",
          "defaultscenario_streaming.bnk"}) {
        os << "worlds\\albion\\" << region << "\\defaultscenario\\" << leaf
           << "\r\n";
    }
    return os.str();
}

bool write_file(const std::filesystem::path& p,
                const std::vector<uint8_t>& bytes, std::string& error) {
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) {
        error = "cannot create " + p.string();
        return false;
    }
    if (!bytes.empty()) {
        f.write(reinterpret_cast<const char*>(bytes.data()),
                (std::streamsize)bytes.size());
    }
    if (!f) {
        error = "write failed for " + p.string();
        return false;
    }
    return true;
}

std::vector<uint8_t> to_bytes(const std::string& s) {
    return std::vector<uint8_t>(s.begin(), s.end());
}

}

bool DeleteCustomLevel(const FlatAssetEntry& entry, std::string& error) {
    if (!GameBackup::RequireBackup(error)) return false;
    std::string region;
    if (!IsCustomLooseLevel(entry, &region) || region.empty()) {
        error = "Only custom levels created by this app can be deleted.";
        return false;
    }
    const std::string data_dir = ResolveGameDataDir();
    if (data_dir.empty()) {
        error = "No game data directory.";
        return false;
    }

    if (!UnregisterLevel(data_dir, region, error)) return false;

    const std::filesystem::path folder =
        std::filesystem::path(data_dir) / "worlds" / "albion" / region;
    std::error_code ec;
    std::filesystem::remove_all(folder, ec);
    if (ec) {
        error = "could not delete " + folder.string() + ": " +
                ec.message();
        return false;
    }

    std::string menu_error;
    if (!SyncDebugMenuCustomLevels(data_dir, menu_error)) {
        OutputLog::warn("delete level: debug menu sync failed: " +
                        menu_error);
    }

    BnkCache::invalidate(entry.bnk_path);
    OutputLog::success("custom level '" + region + "' deleted");
    return true;
}

std::string ResolveGameDataDir() {
    if (S.root_dir.empty()) return {};
    std::error_code ec;
    const std::filesystem::path root(S.root_dir);
    if (!std::filesystem::is_directory(root, ec)) return {};
    if (lower(root.filename().string()) == "data") return root.string();
    const std::filesystem::path data = root / "data";
    if (std::filesystem::is_directory(data, ec)) return data.string();
    return {};
}

bool ValidateLevelName(const std::string& name, std::string& error) {
    if (name.empty()) {
        error = "Enter a level name.";
        return false;
    }
    if (name.size() > 48) {
        error = "Name too long (max 48 characters).";
        return false;
    }
    for (const char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '_';
        if (!ok) {
            error = "Use letters, digits and underscores only.";
            return false;
        }
    }
    return true;
}

bool LevelNameExists(const std::string& name) {
    const std::string data_dir = ResolveGameDataDir();
    if (data_dir.empty()) return false;
    const std::string region = lower(name);
    std::error_code ec;
    if (std::filesystem::exists(std::filesystem::path(data_dir) / "worlds" /
                                    "albion" / region,
                                ec)) {
        return true;
    }
    const std::string prefix = "worlds\\albion\\" + region + "\\";
    for (const char* bank : {"levels.bnk", "streaming.bnk"}) {
        const std::filesystem::path bnk_path =
            std::filesystem::path(data_dir) / bank;
        if (!std::filesystem::is_regular_file(bnk_path, ec)) continue;
        try {
            const BnkCache::Entry bnk = BnkCache::get(bnk_path.string());
            for (const auto& fe : bnk.reader->list_files()) {
                if (lower(backslash(fe.name)).rfind(prefix, 0) == 0) {
                    return true;
                }
            }
        } catch (...) {
        }
    }
    return false;
}

NewLevelResult CreateNewLevel(const NewLevelParams& params) {
    NewLevelResult res;

    if (!GameBackup::RequireBackup(res.error)) return res;
    if (!ValidateLevelName(params.name, res.error)) return res;
    const std::string region = lower(params.name);

    const std::string data_dir = ResolveGameDataDir();
    if (data_dir.empty()) {
        res.error = "No game data directory (open a Fable 2 root first; "
                    "ISO roots are not writable).";
        return res;
    }
    if (LevelNameExists(region)) {
        res.error = "A level named '" + region + "' already exists.";
        return res;
    }

    std::vector<DonorFile> donor_files;
    if (!load_donor_files(data_dir, params.donor_region, donor_files,
                          res.error)) {
        return res;
    }
    const std::string hfid = donor_heightfield_id(donor_files);
    if (hfid.empty()) {
        res.error = "Donor region has no .ghf heightfield to clone.";
        return res;
    }

    std::vector<uint8_t> custom_spawn_save;
    std::vector<SpawnPointMapping> spawn_mappings;
    bool found_spawn_save = false;
    for (const DonorFile& df : donor_files) {
        if (skip_authored_donor_file(df.rel_path)) continue;
        const size_t slash = df.rel_path.find_last_of('\\');
        const std::string leaf = slash == std::string::npos
                                     ? df.rel_path
                                     : df.rel_path.substr(slash + 1);
        if (leaf != "defaultscenario.save") continue;
        if (!build_custom_spawn_save(df.bytes, params.name,
                                     custom_spawn_save, spawn_mappings,
                                     res.error)) {
            return res;
        }
        found_spawn_save = true;
        break;
    }
    if (!found_spawn_save) {
        res.error = "Donor region has no default scenario save file.";
        return res;
    }

    const std::string region_prefix = "worlds\\albion\\" + region + "\\";
    std::vector<std::string> virtual_paths;

    for (const DonorFile& df : donor_files) {
        const size_t slash = df.rel_path.find_last_of('\\');
        const std::string leaf = slash == std::string::npos
                                     ? df.rel_path
                                     : df.rel_path.substr(slash + 1);
        std::vector<uint8_t> bytes;
        if (leaf == "defaultscenario.engine_level") {
            bytes = to_bytes(build_engine_level(region, hfid));
        } else if (leaf == "defaultscenario.list") {
            bytes = to_bytes(build_list_file(region, hfid));
        } else if (leaf == "defaultscenario.save") {
            bytes = custom_spawn_save;
        } else if (leaf == "defaultscenario.gdb") {
            if (!build_custom_spawn_gdb(df.bytes, spawn_mappings, bytes,
                                        res.error)) {
                return res;
            }
        } else if (leaf == "level.vfsconfig" || leaf == "config.ai_config") {
            const std::string text(df.bytes.begin(), df.bytes.end());
            bytes = to_bytes(replace_ci(text, params.donor_region, region));
        } else if (leaf == hfid + ".ghf") {
            if (!build_blank_heightfield(region, hfid, bytes, res.error)) {
                return res;
            }
        } else if (leaf == hfid + ".water") {
            bytes = {0, 0, 0, 2, 0, 0, 0, 0};
        } else if (leaf == hfid + ".mist") {
            bytes = {0, 0, 0, 4, 0, 0, 0, 0};
        } else {
            bytes = df.bytes;
        }

        const std::string vpath = region_prefix + df.rel_path;
        const std::filesystem::path disk =
            std::filesystem::path(data_dir) / backslash(vpath);
        if (!write_file(disk, bytes, res.error)) return res;
        virtual_paths.push_back(vpath);
        res.written_files.push_back(disk.string());
    }

    if (!RegisterInScenariosList(data_dir, region, res.error)) return res;
    if (!RegisterInDirManifest(data_dir, virtual_paths, res.error)) return res;

    std::string menu_error;
    if (!SyncDebugMenuCustomLevels(data_dir, menu_error)) {
        OutputLog::warn("new level: debug menu sync failed: " + menu_error);
    }

    res.engine_level_virtual_path =
        region_prefix + "defaultscenario\\defaultscenario.engine_level";
    res.ok = true;
    OutputLog::success("new level '" + region + "' created (" +
                       std::to_string(res.written_files.size()) +
                       " files under data\\worlds\\albion\\" + region + ")");
    return res;
}

}
}
