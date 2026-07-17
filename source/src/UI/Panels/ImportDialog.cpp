#include "ImportDialog.h"

#include "../OutputLog.h"
#include "../UI_Panels.h"
#include "../ModelPreview.h"
#include "../../Entity/StaticPropAuthoring.h"
#include "../../Import/AssetImport.h"
#include "../../Level/Core/LevelLoader.h"
#include "../../Utilities/GameBackup.h"
#include "../../Utilities/DebugTrace.h"
#include "../../Utilities/Progress.h"
#include "../../Utilities/State.h"

#include "imgui.h"
#include "imgui_stdlib.h"
#include "ImGuiFileDialog.h"

#include <atomic>
#include <cctype>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

namespace ImportDialog {

namespace {

enum class Mode { None, Glb, Image, Folder, TextureReplace };

Mode        s_mode = Mode::None;
std::string s_source_path;
std::string s_asset_name;
std::string s_dest_folder;
std::string s_target_bnk_path;
std::string s_target_file_name;
int         s_target_file_index = -1;
int         s_format_idx = 0;
int         s_max_dim_idx = 2;
bool        s_gen_mips = true;
bool        s_create_template = true;
std::string s_entity_id;
bool        s_open_settings = false;

struct WorkerHolder {
    std::thread t;
    ~WorkerHolder() { if (t.joinable()) t.join(); }
};
WorkerHolder       s_worker_holder;
std::thread&       s_worker = s_worker_holder.t;
std::atomic<bool>  s_busy{false};
std::atomic<bool>  s_done{false};
std::mutex         s_result_mutex;
AssetImport::Result s_result;
std::string        s_error;
bool               s_ok = false;
Mode               s_done_mode = Mode::None;

const char* kFormatNames[] = {
    "Auto (BC1, BC3 when alpha)", "BC1 (opaque)", "BC3 (alpha)",
    "BC5 (normal map)", "Raw ARGB8 (experimental)",
};
const int kMaxDims[] = {256, 512, 1024, 2048, 4096};

TexWriter::Format format_from_idx(int idx) {
    switch (idx) {
        case 1: return TexWriter::Format::BC1;
        case 2: return TexWriter::Format::BC3;
        case 3: return TexWriter::Format::BC5Normal;
        case 4: return TexWriter::Format::RawARGB;
        default: return TexWriter::Format::Auto;
    }
}

void open_picker(Mode mode) {
    if (s_busy.load()) {
        OutputLog::warn("import: an import is already running");
        return;
    }
    s_mode = mode;
    IGFD::FileDialogConfig cfg;
    cfg.path = S.export_dir.empty() ? "." : S.export_dir;
    switch (mode) {
        case Mode::Glb:
            ImGuiFileDialog::Instance()->OpenDialog(
                "ImportPickGlb", "Select .glb model",
                "glTF binary (*.glb){.glb},.*", cfg);
            break;
        case Mode::Image:
            ImGuiFileDialog::Instance()->OpenDialog(
                "ImportPickImage", "Select image",
                "Images (*.png *.jpg *.jpeg *.dds *.tif *.tiff *.bmp *.tga "
                "*.psd){.png,.jpg,.jpeg,.dds,.tif,.tiff,.bmp,.tga,.psd},.*",
                cfg);
            break;
        case Mode::TextureReplace:
            ImGuiFileDialog::Instance()->OpenDialog(
                "ReplaceTexturePickImage", "Select replacement image",
                "Images (*.png *.jpg *.jpeg *.dds *.tif *.tiff *.bmp *.tga "
                "*.psd){.png,.jpg,.jpeg,.dds,.tif,.tiff,.bmp,.tga,.psd},.*",
                cfg);
            break;
        case Mode::Folder:
            ImGuiFileDialog::Instance()->OpenDialog(
                "ImportPickFolder", "Select folder to import", nullptr, cfg);
            break;
        default: break;
    }
}

void prime_settings_from_source() {
    const std::string stem =
        std::filesystem::path(s_source_path).stem().string();
    s_asset_name = AssetImport::sanitize_name(stem);
    s_max_dim_idx = 2;
    switch (s_mode) {
        case Mode::Glb:
            s_dest_folder = "art\\imported\\" + s_asset_name;
            break;
        case Mode::Image:
            s_dest_folder = "art\\imported";
            break;
        case Mode::Folder:
            s_dest_folder.clear();
            break;
        case Mode::TextureReplace:
            s_asset_name = std::filesystem::path(s_target_file_name)
                               .stem().string();
            s_dest_folder.clear();
            s_max_dim_idx = 4;
            break;
        default: break;
    }
    s_format_idx = 0;
    s_gen_mips = true;
    s_create_template =
        (s_mode == Mode::Glb || s_mode == Mode::Folder);
    s_entity_id.clear();
    if (s_mode == Mode::Glb) {
        s_entity_id = "PROP_" + s_asset_name;
        for (char& c : s_entity_id) {
            if (!std::isalnum((unsigned char)c) && c != '_') c = '_';
        }
    }
    s_open_settings = true;
}

void launch_worker() {
    AssetImport::Options opt;
    opt.dest_folder = s_dest_folder;
    opt.asset_name = (s_mode == Mode::Folder) ? std::string() : s_asset_name;
    opt.tex_format = format_from_idx(s_format_idx);
    opt.max_tex_dim = kMaxDims[s_max_dim_idx];
    opt.generate_mips = s_gen_mips;
    opt.create_gdb_template =
        s_create_template && (s_mode == Mode::Glb || s_mode == Mode::Folder);
    opt.entity_id = (s_mode == Mode::Glb) ? s_entity_id : std::string();

    const std::string src = s_source_path;
    const Mode mode = s_mode;
    const std::string target_bnk = s_target_bnk_path;
    const int target_index = s_target_file_index;

    s_busy.store(true);
    s_done.store(false);
    if (s_worker.joinable()) s_worker.join();
    s_worker = std::thread([src, opt, mode, target_bnk, target_index]() {
        progress_open(100,
                      std::string(mode == Mode::TextureReplace
                                      ? "Replacing " : "Importing ") +
                               std::filesystem::path(src).filename().string());
        AssetImport::Result res;
        std::string err;
        bool ok = false;
        try {
            switch (mode) {
                case Mode::Glb:
                    ok = AssetImport::import_glb(src, opt, res, err);
                    break;
                case Mode::Image:
                    ok = AssetImport::import_image(src, opt, res, err);
                    break;
                case Mode::Folder:
                    ok = AssetImport::import_folder(src, opt, res, err);
                    break;
                case Mode::TextureReplace:
                    ok = AssetImport::replace_texture(
                        src, target_bnk, target_index, opt, res, err);
                    break;
                default:
                    err = "no import mode";
                    break;
            }
        } catch (const std::exception& ex) {
            ok = false;
            err = std::string("unhandled exception: ") + ex.what();
            if (mode == Mode::TextureReplace) {
                DebugTrace::log(
                    "texture-replace: worker exception='%s'", ex.what());
            }
        } catch (...) {
            ok = false;
            err = "unhandled exception";
            if (mode == Mode::TextureReplace) {
                DebugTrace::log("texture-replace: worker unknown exception");
            }
        }
        progress_done();
        {
            std::lock_guard<std::mutex> lk(s_result_mutex);
            s_result = std::move(res);
            s_error = err;
            s_ok = ok;
            s_done_mode = mode;
        }
        s_busy.store(false);
        s_done.store(true);
    });
}

void draw_settings_modal() {
    if (s_open_settings) {
        ImGui::OpenPopup("Import Settings");
        s_open_settings = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Import Settings", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::TextUnformatted("Source:");
    ImGui::SameLine();
    ImGui::TextWrapped("%s", s_source_path.c_str());
    ImGui::Separator();

    if (s_mode == Mode::TextureReplace) {
        ImGui::TextUnformatted("Target:");
        ImGui::SameLine();
        ImGui::TextWrapped("%s", s_target_file_name.c_str());
        ImGui::TextDisabled(
            "Auto preserves the existing texture format.");
    } else if (s_mode != Mode::Folder) {
        ImGui::InputText("Asset name", &s_asset_name);
        ImGui::InputText("Destination folder", &s_dest_folder);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Virtual folder inside the game data.\n"
                "The .mdl and its textures are injected into the Globals\n"
                "model/texture BNK pairs under this path.");
        }
    } else {
        ImGui::InputText("Destination folder (empty = per-asset)",
                         &s_dest_folder);
        ImGui::TextDisabled(
            "Every .glb and image directly inside the folder is imported.");
    }

    ImGui::Combo("Texture format", &s_format_idx, kFormatNames,
                 IM_ARRAYSIZE(kFormatNames));
    const char* dim_names[] = {"256", "512", "1024", "2048", "4096"};
    ImGui::Combo("Max texture size", &s_max_dim_idx, dim_names,
                 IM_ARRAYSIZE(dim_names));
    ImGui::Checkbox("Generate mip chain (down to 16px, like retail)",
                    &s_gen_mips);

    bool entity_id_ok = true;
    if (s_mode == Mode::Glb || s_mode == Mode::Folder) {
        ImGui::Separator();
        ImGui::Checkbox("Create spawnable entity in globals.gdb",
                        &s_create_template);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Authors a static-prop template (model + transform) so the\n"
                "model can be placed in levels and spawns in game.");
        }
        if (s_create_template) {
            if (s_mode == Mode::Glb) {
                ImGui::InputTextWithHint("Entity ID", "PROP_MyModel",
                                         &s_entity_id);
                entity_id_ok =
                    StaticPropAuthoring::IsValidInternalName(s_entity_id);
                if (!entity_id_ok) {
                    ImGui::TextColored(
                        ImVec4(0.95f, 0.42f, 0.38f, 1.0f),
                        "Entity ID must start with a letter or underscore "
                        "and use only letters, numbers, underscores.");
                }
            } else {
                ImGui::TextDisabled(
                    "Each imported .glb gets a PROP_<name> entity.");
            }
        }
    }

    ImGui::Separator();
    const bool can_import =
        !s_busy.load() && entity_id_ok &&
        (s_mode == Mode::Folder || s_mode == Mode::TextureReplace ||
         !s_asset_name.empty());
    if (!can_import) ImGui::BeginDisabled();
    if (ImGui::Button(s_mode == Mode::TextureReplace ? "Replace" : "Import",
                      ImVec2(120, 0))) {
        launch_worker();
        ImGui::CloseCurrentPopup();
    }
    if (!can_import) ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
        s_mode = Mode::None;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void poll_worker() {
    if (!s_done.exchange(false)) return;
    if (s_worker.joinable()) s_worker.join();

    std::lock_guard<std::mutex> lk(s_result_mutex);
    if (!s_ok) {
        OutputLog::error(
            std::string(s_done_mode == Mode::TextureReplace
                            ? "texture replace failed: " : "import failed: ") +
            s_error);
        return;
    }
    const char* prefix =
        s_done_mode == Mode::TextureReplace ? "texture: " : "import: ";
    for (const auto& n : s_result.notes) OutputLog::info(prefix + n);
    if (s_done_mode == Mode::TextureReplace) {
        MP_TextureCache_Clear();
        OutputLog::success("texture: replacement complete");
        return;
    }
    if (s_result.gdb_template_created) {
        // catalog rebuild on the UI thread so the Add-Entity list picks the
        // new prop up immediately
        Level::BuildGlobalEntityCatalog();
        OutputLog::success(
            "import: spawnable entity " + s_result.entity_id +
            " is ready - enable Edit Level and use the Add menu (or the "
            "entity list) to place it, then save the level.");
    }
    if (!s_result.mdl_virtual_path.empty()) {
        OutputLog::info(
            "import: done - find the model under " +
            s_result.mdl_virtual_path +
            " in the file tree (Globals BNKs). Re-importing to the same "
            "name replaces it.");
    } else if (!s_result.tex_virtual_paths.empty()) {
        OutputLog::info("import: done - " +
                        std::to_string(s_result.tex_virtual_paths.size()) +
                        " texture(s) injected.");
    }
}

}

void OpenGlb()    { open_picker(Mode::Glb); }
void OpenImage()  { open_picker(Mode::Image); }
void OpenFolder() { open_picker(Mode::Folder); }
void OpenTextureReplacement(const std::string& bnk_path, int file_index,
                            const std::string& file_name) {
    std::string error;
    if (!GameBackup::RequireBackup(error)) {
        OutputLog::error("texture: " + error);
        return;
    }
    if (s_busy.load()) return;
    s_target_bnk_path = bnk_path;
    s_target_file_index = file_index;
    s_target_file_name = file_name;
    open_picker(Mode::TextureReplace);
}
bool Busy()       { return s_busy.load(); }

void Draw() {
    ImVec2 vp = ImGui::GetMainViewport()->WorkSize;
    ImVec2 minSize(680, 440);
    ImVec2 maxSize(vp.x * 0.9f, vp.y * 0.9f);

    auto handle_picker = [&](const char* key) {
        if (ImGuiFileDialog::Instance()->Display(
                key, ImGuiWindowFlags_NoCollapse, minSize, maxSize)) {
            if (ImGuiFileDialog::Instance()->IsOk()) {
                s_source_path =
                    (s_mode == Mode::Folder)
                        ? ImGuiFileDialog::Instance()->GetCurrentPath()
                        : ImGuiFileDialog::Instance()->GetFilePathName();
                prime_settings_from_source();
            } else {
                s_mode = Mode::None;
            }
            ImGuiFileDialog::Instance()->Close();
        }
    };
    handle_picker("ImportPickGlb");
    handle_picker("ImportPickImage");
    handle_picker("ImportPickFolder");
    handle_picker("ReplaceTexturePickImage");

    draw_settings_modal();
    poll_worker();
    tree_apply_pending_injections();
}

}
