#include "SkyAuthoring.h"

#include "LandscapeAuthoring.h"
#include "NewLevel.h"

#include "BNKCore.cpp"
#include "GDB/GdbEdit.h"
#include "Level/Core/LevelLoader.h"
#include "UI/OutputLog.h"
#include "Utilities/GameBackup.h"
#include "Utilities/State.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <unordered_set>

namespace Level {
namespace Creation {

namespace {

constexpr uint32_t kHashEnvironmentThemeDaySet = 0x0843AB41u;

bool read_file_bytes(const std::filesystem::path& p,
                     std::vector<uint8_t>& out) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f) return false;
    const std::streamoff len = f.tellg();
    if (len <= 0) return false;
    out.resize((size_t)len);
    f.seekg(0);
    return bool(f.read(reinterpret_cast<char*>(out.data()), len));
}


std::filesystem::path level_gdb_path(const FlatAssetEntry& entry) {
    if (entry.bnk_path.empty()) return {};
    std::error_code ec;
    if (!std::filesystem::is_directory(entry.bnk_path, ec)) return {};
    const std::filesystem::path scenario_dir =
        (std::filesystem::path(entry.bnk_path) / entry.full_path)
            .parent_path();
    for (std::filesystem::directory_iterator it(scenario_dir, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        std::string ext = it->path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) {
                           return (char)std::tolower(c);
                       });
        if (ext == ".gdb") return it->path();
    }
    return {};
}

}




bool ListSkyThemes(std::vector<SkyThemeOption>& out, std::string& error) {
    out.clear();
    const std::string data_dir = ResolveGameDataDir();
    if (data_dir.empty()) {
        error = "open a game folder first";
        return false;
    }

    std::unordered_set<uint32_t> seen;
    for (const FlatAssetEntry& e : S.all_gdb_files) {
        std::string low = e.full_path;
        std::transform(low.begin(), low.end(), low.begin(),
                       [](unsigned char c) {
                           if (c == '/') return '\\';
                           return (char)std::tolower(c);
                       });
        const std::string prefix = "worlds\\albion\\";
        if (low.rfind(prefix, 0) != 0) continue;
        const size_t region_end = low.find('\\', prefix.size());
        if (region_end == std::string::npos) continue;
        const std::string region =
            low.substr(prefix.size(), region_end - prefix.size());
        if (region.empty()) continue;

        std::vector<uint8_t> bytes;
        try {
            bytes = BnkCache::extract_bytes(e.bnk_path, e.file_index);
        } catch (...) {
            continue;
        }
        GdbEdit::GdbFile gdb;
        std::string err;
        if (!gdb.Parse(bytes, err)) continue;

        for (size_t i = 0; i < gdb.RecordCount(); ++i) {
            GdbEdit::Field f;
            if (!gdb.FindLocalField(gdb.RecordAt(i).hash,
                                    kHashEnvironmentThemeDaySet, f)) {
                continue;
            }
            if (f.value == 0 || !seen.insert(f.value).second) break;
            SkyThemeOption option;
            option.day_set_hash = f.value;
            option.name = region + " sky";
            out.push_back(std::move(option));
            break;
        }
    }

    std::sort(out.begin(), out.end(),
              [](const SkyThemeOption& a, const SkyThemeOption& b) {
                  return a.name < b.name;
              });
    if (out.empty()) {
        error = "no level GDBs with sky themes indexed yet (wait for the "
                "file index to finish)";
        return false;
    }
    return true;
}

uint32_t CurrentSkyTheme(const FlatAssetEntry& entry) {
    const std::filesystem::path gdb_path = level_gdb_path(entry);
    std::vector<uint8_t> bytes;
    if (gdb_path.empty() || !read_file_bytes(gdb_path, bytes)) return 0;
    GdbEdit::GdbFile gdb;
    std::string err;
    if (!gdb.Parse(bytes, err)) return 0;
    for (size_t i = 0; i < gdb.RecordCount(); ++i) {
        GdbEdit::Field f;
        if (gdb.FindLocalField(gdb.RecordAt(i).hash,
                               kHashEnvironmentThemeDaySet, f)) {
            return f.value;
        }
    }
    return 0;
}

bool ApplySkyTheme(const FlatAssetEntry& entry, uint32_t day_set_hash,
                   std::string& error) {
    if (!GameBackup::RequireBackup(error)) return false;
    if (!IsCustomLooseLevel(entry)) {
        error = "Sky authoring only works on loose custom levels.";
        return false;
    }
    const std::filesystem::path gdb_path = level_gdb_path(entry);
    std::vector<uint8_t> bytes;
    if (gdb_path.empty() || !read_file_bytes(gdb_path, bytes)) {
        error = "level GDB not found next to the .engine_level";
        return false;
    }
    GdbEdit::GdbFile gdb;
    if (!gdb.Parse(bytes, error)) {
        error = "level GDB parse failed: " + error;
        return false;
    }

    uint32_t owner_rec = 0;
    for (size_t i = 0; i < gdb.RecordCount(); ++i) {
        GdbEdit::Field f;
        if (gdb.FindLocalField(gdb.RecordAt(i).hash,
                               kHashEnvironmentThemeDaySet, f)) {
            owner_rec = gdb.RecordAt(i).hash;
            break;
        }
    }
    if (owner_rec == 0) {
        error = "level GDB has no EnvironmentThemeDaySet field to patch";
        return false;
    }
    if (!gdb.SetFieldValue(owner_rec, kHashEnvironmentThemeDaySet,
                           day_set_hash)) {
        error = "could not set the day-set field";
        return false;
    }

    const std::vector<uint8_t> out = gdb.Serialize();
    const std::filesystem::path tmp = gdb_path.string() + ".sky_tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f ||
            !f.write(reinterpret_cast<const char*>(out.data()),
                     (std::streamsize)out.size())) {
            error = "could not write " + tmp.string();
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, gdb_path, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        error = "could not replace " + gdb_path.string();
        return false;
    }

    BnkCache::invalidate(entry.bnk_path);
    OutputLog::success("sky: theme applied; reloading level");
    Level::OpenAsync(entry);
    return true;
}

}
}
