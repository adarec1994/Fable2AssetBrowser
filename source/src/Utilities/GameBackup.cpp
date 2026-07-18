#include "GameBackup.h"

#include "State.h"
#include "../BNKCore.cpp"
#include "../ISO/IsoMount.h"
#include "../UI/OutputLog.h"
#include "../UI/Panels/PanelInternal.h"

#include "imgui.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

namespace GameBackup {

namespace {

std::atomic<bool> g_busy{false};
std::atomic<bool> g_finished{false};   
std::atomic<bool> g_last_ok{false};
std::atomic<bool> g_was_restore{false};
std::mutex        g_status_mutex;
std::string       g_status;

void set_status(const std::string& s) {
    std::lock_guard<std::mutex> lock(g_status_mutex);
    g_status = s;
}

std::filesystem::path game_root() {
    if (S.root_dir.empty() || ISO::IsoMount::is_iso_path(S.root_dir)) {
        return {};
    }
    std::error_code ec;
    const std::filesystem::path root(S.root_dir);
    return std::filesystem::is_directory(root, ec) ? root
                                                   : std::filesystem::path();
}

std::filesystem::path backup_dir() {
    const std::filesystem::path root = game_root();
    return root.empty() ? root : root / "f2ab_backup";
}

std::filesystem::path manifest_path() {
    const std::filesystem::path dir = backup_dir();
    return dir.empty() ? dir : dir / "backup.manifest";
}



std::vector<std::string> writable_files() {
    std::vector<std::string> out = {
        "data/levels.bnk",
        "data/streaming.bnk",
        "data/gamescripts.bnk",
        "data/gamescripts_r.bnk",
        "data/scenarios.list",
        "data/dir.manifest",
        "data/miscellaneous/fasttravellist.txt",
        "data/Globals/Globals.gdb",
        "data/scripts/Mods/DebugMenuMod/DebugMenuEntries.lua",
    };
    
    const std::filesystem::path root = game_root();
    std::error_code ec;
    const std::filesystem::path language = root / "data" / "language";
    if (std::filesystem::is_directory(language, ec)) {
        for (std::filesystem::directory_iterator it(language, ec), end;
             !ec && it != end; it.increment(ec)) {
            const std::filesystem::path babel =
                it->path() / "text" / "book.babel";
            if (std::filesystem::is_regular_file(babel, ec)) {
                out.push_back("data/language/" +
                              it->path().filename().string() +
                              "/text/book.babel");
            }
        }
    }
    const std::filesystem::path data = root / "data";
    for (std::filesystem::recursive_directory_iterator it(data, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        std::string ext = it->path().extension().string();
        std::string leaf = it->path().filename().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });
        std::transform(leaf.begin(), leaf.end(), leaf.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });
        if (ext != ".bnk" || leaf.find("texture") == std::string::npos) {
            continue;
        }
        const std::filesystem::path rel =
            std::filesystem::relative(it->path(), root, ec);
        if (ec) break;
        const std::string rel_text = rel.generic_string();
        if (std::find(out.begin(), out.end(), rel_text) == out.end()) {
            out.push_back(rel_text);
        }
    }
    return out;
}

bool copy_with_status(const std::filesystem::path& from,
                      const std::filesystem::path& to, const char* verb,
                      size_t index, size_t count) {
    std::error_code ec;
    std::filesystem::create_directories(to.parent_path(), ec);
    set_status(std::string(verb) + " " + from.filename().string() + " (" +
               std::to_string(index + 1) + "/" + std::to_string(count) +
               ")...");
    std::filesystem::copy_file(
        from, to, std::filesystem::copy_options::overwrite_existing, ec);
    return !ec;
}

void worker(bool restore) {
    const std::filesystem::path root = game_root();
    const std::filesystem::path dir = backup_dir();
    bool ok = !root.empty();
    std::vector<std::string> files;

    if (ok && restore) {
        std::ifstream manifest(manifest_path());
        std::string line;
        while (std::getline(manifest, line)) {
            while (!line.empty() &&
                   (line.back() == '\r' || line.back() == '\n')) {
                line.pop_back();
            }
            if (!line.empty()) files.push_back(line);
        }
        ok = !files.empty();
    } else if (ok) {
        std::error_code ec;
        for (const std::string& rel : writable_files()) {
            if (std::filesystem::is_regular_file(root / rel, ec)) {
                files.push_back(rel);
            }
        }
        ok = !files.empty();
    }

    for (size_t i = 0; ok && i < files.size(); ++i) {
        const std::filesystem::path original = root / files[i];
        const std::filesystem::path saved = dir / files[i];
        ok = restore
                 ? copy_with_status(saved, original, "Restoring", i,
                                    files.size())
                 : copy_with_status(original, saved, "Backing up", i,
                                    files.size());
    }

    if (ok && !restore) {
        
        std::ofstream manifest(manifest_path(), std::ios::trunc);
        for (const std::string& rel : files) manifest << rel << "\n";
        ok = manifest.good();
    }

    set_status(ok ? (restore ? "Restore complete."
                             : "Backup complete.")
                  : "FAILED - check disk space and file locks.");
    g_last_ok = ok;
    g_was_restore = restore;
    g_finished = true;
    g_busy = false;
}

void start(bool restore) {
    if (g_busy.exchange(true)) return;
    g_finished = false;
    std::thread(worker, restore).detach();
}

}

bool Exists() {
    const std::filesystem::path manifest = manifest_path();
    if (manifest.empty()) return false;
    std::error_code ec;
    return std::filesystem::is_regular_file(manifest, ec);
}

bool Busy() { return g_busy.load(); }

std::string StatusText() {
    std::lock_guard<std::mutex> lock(g_status_mutex);
    return g_status;
}

bool RequireBackup(std::string& error) {
    if (game_root().empty()) {
        error = "Open a Fable 2 game folder first (ISO roots are "
                "read-only).";
        return false;
    }
    if (Busy()) {
        error = "Wait for the backup operation to finish.";
        return false;
    }
    if (!Exists()) {
        error = "Editing is disabled until you create a backup - use "
                "Backup > Create Backup in the top menu bar.";
        return false;
    }
    return true;
}

bool EnsureFilesCovered(const std::vector<std::string>& paths,
                        std::string& error) {
    if (!RequireBackup(error)) return false;
    const std::filesystem::path root = game_root();
    const std::filesystem::path dir = backup_dir();
    std::set<std::string> manifest_entries;
    {
        std::ifstream manifest(manifest_path());
        std::string line;
        while (std::getline(manifest, line)) {
            while (!line.empty() &&
                   (line.back() == '\r' || line.back() == '\n')) {
                line.pop_back();
            }
            if (!line.empty()) manifest_entries.insert(line);
        }
    }
    std::vector<std::string> additions;
    for (const std::string& path_text : paths) {
        std::error_code ec;
        const std::filesystem::path absolute =
            std::filesystem::weakly_canonical(path_text, ec);
        const std::filesystem::path canonical_root =
            std::filesystem::weakly_canonical(root, ec);
        if (ec || absolute.empty() || canonical_root.empty()) {
            error = "Could not resolve a texture bank for backup.";
            return false;
        }
        const std::filesystem::path rel =
            std::filesystem::relative(absolute, canonical_root, ec);
        if (ec || rel.empty() || rel.is_absolute() ||
            *rel.begin() == "..") {
            error = "Texture replacement target is outside the game folder.";
            return false;
        }
        const std::string rel_text = rel.generic_string();
        const std::filesystem::path saved = dir / rel;
        if (manifest_entries.count(rel_text) &&
            std::filesystem::is_regular_file(saved, ec)) {
            continue;
        }
        std::filesystem::create_directories(saved.parent_path(), ec);
        if (ec) {
            error = "Could not create the texture backup folder: " +
                    ec.message();
            return false;
        }
        std::filesystem::copy_file(
            absolute, saved,
            std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            error = "Could not back up " + absolute.string() + ": " +
                    ec.message();
            return false;
        }
        manifest_entries.insert(rel_text);
        additions.push_back(rel_text);
    }
    if (!additions.empty()) {
        std::ofstream manifest(manifest_path(), std::ios::app);
        for (const std::string& rel : additions) manifest << rel << '\n';
        if (!manifest) {
            error = "Could not update the backup manifest.";
            return false;
        }
    }
    return true;
}

void CreateAsync() {
    if (game_root().empty()) {
        OutputLog::error("backup: open a game folder first");
        return;
    }
    OutputLog::info("backup: creating pristine copy under f2ab_backup\\ "
                    "(this copies the large game banks - please wait)");
    start(false);
}

void RestoreAsync() {
    if (!Exists()) return;
    OutputLog::info("backup: restoring pristine game files...");
    start(true);
}

void DrawMainMenu() {
    
    if (g_finished.exchange(false)) {
        if (g_last_ok) {
            OutputLog::success("backup: " + StatusText());
            if (g_was_restore) {
                BnkCache::clear();
                g_tree_last_root_dir.clear();
                g_tree_built.store(false);
                OutputLog::info(
                    "backup: caches cleared - the file index will "
                    "rebuild; reopen levels/quests as needed");
            }
        } else {
            OutputLog::error("backup: " + StatusText());
        }
    }

    const bool exists = Exists();
    const bool busy = Busy();
    
    const char* label = busy ? "Backup...###bp_backup"
                       : exists ? "Backup###bp_backup"
                                : "Backup!###bp_backup";
    if (!exists && !busy) {
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImVec4(0.98f, 0.62f, 0.30f, 1.0f));
    }
    const bool menu_open = ImGui::BeginMenu(label);
    if (!exists && !busy) ImGui::PopStyleColor();
    bool open_restore_confirm = false;
    if (menu_open) {
        if (busy) {
            ImGui::TextDisabled("%s", StatusText().c_str());
        } else if (!exists) {
            if (ImGui::MenuItem("Create Backup")) CreateAsync();
        } else {
            if (ImGui::MenuItem("Restore Backup")) {
                open_restore_confirm = true;
            }
        }
        ImGui::EndMenu();
    }
    if (open_restore_confirm) {
        ImGui::OpenPopup("Restore original game files?##bp_restore");
    }
    if (ImGui::BeginPopupModal("Restore original game files?##bp_restore",
                               nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(
            "This overwrites the current game files with the pristine\n"
            "backup - injected quests, level edits and text changes are\n"
            "undone. Custom level folders stay on disk but become\n"
            "unregistered.");
        if (ImGui::Button("Restore", ImVec2(140, 0))) {
            RestoreAsync();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(140, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

}
