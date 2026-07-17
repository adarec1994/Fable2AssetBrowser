#include "NewLevel.h"
#include "GameRegistry.h"
#include "LandscapeAuthoring.h"

#include "BNKCore.cpp"
#include "UI/OutputLog.h"
#include "Utilities/GameBackup.h"
#include "Utilities/State.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

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
    for (const char* ext : {"ghf", "ama", "amm", "amr", "hdb", "genv"}) {
        os << "Worlds\\Albion\\" << region << "\\" << hfid << "." << ext
           << "\r\n";
    }
    os << "Worlds\\Albion\\" << region
       << "\\DefaultScenario\\DefaultScenario.gdb\r\n";
    os << "Worlds\\Albion\\" << region
       << "\\DefaultScenario\\DefaultScenario.save\r\n";
    os << "Worlds\\Albion\\" << region
       << "\\DefaultScenario\\pathdata\\Config.ai_config\r\n";
    for (const char* leaf :
         {"defaultscenario.engine_level", "defaultscenario.havok_scenario",
          "defaultscenario_models.bnk", "defaultscenario_textures.bnk",
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
        } else if (leaf == "level.vfsconfig" || leaf == "config.ai_config") {
            const std::string text(df.bytes.begin(), df.bytes.end());
            bytes = to_bytes(replace_ci(text, params.donor_region, region));
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
