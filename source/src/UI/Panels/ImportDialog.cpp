#include "ImportDialog.h"

#include "../OutputLog.h"
#include "../UI_Panels.h"
#include "../../Import/AssetImport.h"
#include "../../Utilities/Progress.h"
#include "../../Utilities/State.h"

#include "imgui.h"
#include "imgui_stdlib.h"
#include "ImGuiFileDialog.h"

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

namespace ImportDialog {

namespace {

enum class Mode { None, Glb, Image, Folder };

Mode        s_mode = Mode::None;
std::string s_source_path;
std::string s_asset_name;
std::string s_dest_folder;
int         s_format_idx = 0;
int         s_max_dim_idx = 2;
bool        s_gen_mips = true;
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
const int kMaxDims[] = {256, 512, 1024, 2048};

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
        default: break;
    }
    s_format_idx = 0;
    s_gen_mips = true;
    s_open_settings = true;
}

void launch_worker() {
    AssetImport::Options opt;
    opt.dest_folder = s_dest_folder;
    opt.asset_name = (s_mode == Mode::Folder) ? std::string() : s_asset_name;
    opt.tex_format = format_from_idx(s_format_idx);
    opt.max_tex_dim = kMaxDims[s_max_dim_idx];
    opt.generate_mips = s_gen_mips;

    const std::string src = s_source_path;
    const Mode mode = s_mode;

    s_busy.store(true);
    s_done.store(false);
    if (s_worker.joinable()) s_worker.join();
    s_worker = std::thread([src, opt, mode]() {
        progress_open(100, "Importing " +
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
                default:
                    err = "no import mode";
                    break;
            }
        } catch (const std::exception& ex) {
            ok = false;
            err = std::string("unhandled exception: ") + ex.what();
        } catch (...) {
            ok = false;
            err = "unhandled exception";
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

    if (s_mode != Mode::Folder) {
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
    const char* dim_names[] = {"256", "512", "1024", "2048"};
    ImGui::Combo("Max texture size", &s_max_dim_idx, dim_names,
                 IM_ARRAYSIZE(dim_names));
    ImGui::Checkbox("Generate mip chain (down to 16px, like retail)",
                    &s_gen_mips);

    ImGui::Separator();
    const bool can_import = !s_busy.load() &&
                            (s_mode == Mode::Folder || !s_asset_name.empty());
    if (!can_import) ImGui::BeginDisabled();
    if (ImGui::Button("Import", ImVec2(120, 0))) {
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
        OutputLog::error("import failed: " + s_error);
        return;
    }
    for (const auto& n : s_result.notes) OutputLog::info("import: " + n);
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

    draw_settings_modal();
    poll_worker();
    tree_apply_pending_injections();
}

}
