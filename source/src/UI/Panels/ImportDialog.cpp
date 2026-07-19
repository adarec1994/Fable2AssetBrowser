#include "ImportDialog.h"

#include "../OutputLog.h"
#include "../UI_Panels.h"
#include "../ModelPreview.h"
#include "../../Entity/StaticPropAuthoring.h"
#include "../../Import/AssetImport.h"
#include "../../Import/GlbImport.h"
#include "../../Import/ImageLoad.h"
#include "../../Level/Core/LevelLoader.h"
#include "../../Utilities/GameBackup.h"
#include "../../Utilities/Progress.h"
#include "../../Utilities/State.h"

#include "imgui.h"
#include "imgui_stdlib.h"
#include "ImGuiFileDialog.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <GL/glew.h>
#endif

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

struct EmbeddedTexturePreview {
    std::string name;
    std::string error;
    int source_width = 0;
    int source_height = 0;
    int preview_width = 0;
    int preview_height = 0;
    std::vector<uint8_t> rgba;
#ifdef _WIN32
    ID3D11ShaderResourceView* srv = nullptr;
#else
    unsigned int texture = 0;
#endif
};

GlbImport::Scene s_glb_scene;
std::vector<AssetImport::MaterialTextureAssignment> s_material_textures;
std::vector<EmbeddedTexturePreview> s_texture_previews;
std::string s_glb_error;
int s_selected_material = 0;

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

void release_texture_previews() {
    for (auto& preview : s_texture_previews) {
#ifdef _WIN32
        if (preview.srv) {
            preview.srv->Release();
            preview.srv = nullptr;
        }
#else
        if (preview.texture) {
            glDeleteTextures(1, &preview.texture);
            preview.texture = 0;
        }
#endif
    }
}

void clear_glb_editor() {
    release_texture_previews();
    s_glb_scene = GlbImport::Scene{};
    s_material_textures.clear();
    s_texture_previews.clear();
    s_glb_error.clear();
    s_selected_material = 0;
}

std::vector<uint8_t> make_thumbnail(const ImageLoad::Image& image,
                                    int& width, int& height) {
    constexpr int kMaxPreviewDimension = 256;
    if (image.width <= 0 || image.height <= 0 || image.rgba.empty()) {
        width = 0;
        height = 0;
        return {};
    }
    const float scale = std::min(
        1.0f, float(kMaxPreviewDimension) /
                  float(std::max(image.width, image.height)));
    width = std::max(1, int(float(image.width) * scale));
    height = std::max(1, int(float(image.height) * scale));
    if (width == image.width && height == image.height) return image.rgba;

    std::vector<uint8_t> result(size_t(width) * size_t(height) * 4);
    for (int y = 0; y < height; ++y) {
        const int source_y = std::min(
            image.height - 1, int((int64_t(y) * image.height) / height));
        for (int x = 0; x < width; ++x) {
            const int source_x = std::min(
                image.width - 1, int((int64_t(x) * image.width) / width));
            const size_t source =
                (size_t(source_y) * size_t(image.width) + size_t(source_x)) * 4;
            const size_t target =
                (size_t(y) * size_t(width) + size_t(x)) * 4;
            std::copy_n(image.rgba.data() + source, 4,
                        result.data() + target);
        }
    }
    return result;
}

void load_glb_editor() {
    clear_glb_editor();
    if (!AssetImport::load_model_scene(s_source_path, s_glb_scene,
                                       s_glb_error)) {
        return;
    }

    s_material_textures.reserve(s_glb_scene.materials.size());
    for (const auto& material : s_glb_scene.materials) {
        AssetImport::MaterialTextureAssignment assignment;
        assignment.diffuse = material.base_color;
        assignment.normal = material.normal;
        assignment.specular = material.occlusion;
        assignment.metallic = material.metallic_rough;
        assignment.extra = material.emissive;
        s_material_textures.push_back(assignment);
    }

    s_texture_previews.reserve(s_glb_scene.images.size());
    for (const auto& image : s_glb_scene.images) {
        EmbeddedTexturePreview preview;
        preview.name = image.name;
        ImageLoad::Image decoded;
        std::string error;
        if (!ImageLoad::load_memory(image.bytes.data(), image.bytes.size(),
                                    image.name, decoded, error)) {
            preview.error = error;
        } else {
            preview.source_width = decoded.width;
            preview.source_height = decoded.height;
            preview.rgba = make_thumbnail(
                decoded, preview.preview_width, preview.preview_height);
        }
        s_texture_previews.push_back(std::move(preview));
    }
}

#ifdef _WIN32
void ensure_texture_previews(ID3D11Device* device) {
    if (!device) return;
    for (auto& preview : s_texture_previews) {
        if (!preview.srv && preview.error.empty() && !preview.rgba.empty()) {
            preview.srv = create_srv_from_rgba(
                device, preview.preview_width, preview.preview_height,
                preview.rgba);
        }
    }
}

ImTextureID preview_texture_id(const EmbeddedTexturePreview& preview) {
    return (ImTextureID)(intptr_t)preview.srv;
}
#else
void ensure_texture_previews() {
    for (auto& preview : s_texture_previews) {
        if (preview.texture || !preview.error.empty() || preview.rgba.empty())
            continue;
        glGenTextures(1, &preview.texture);
        glBindTexture(GL_TEXTURE_2D, preview.texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, preview.preview_width,
                     preview.preview_height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     preview.rgba.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

ImTextureID preview_texture_id(const EmbeddedTexturePreview& preview) {
    return (ImTextureID)(intptr_t)preview.texture;
}
#endif

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
                "ImportPickGlb", "Select model",
                "Models (*.glb *.obj){.glb,.obj},.*", cfg);
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
    clear_glb_editor();
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
        load_glb_editor();
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
    if (s_mode == Mode::Glb) {
        opt.material_textures = s_material_textures;
    }

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

std::string texture_choice_name(int index) {
    if (index < 0) return "None";
    if (size_t(index) >= s_texture_previews.size()) return "Invalid texture";
    const auto& preview = s_texture_previews[index];
    std::string name = preview.name.empty()
                           ? "image_" + std::to_string(index)
                           : preview.name;
    if (preview.source_width > 0 && preview.source_height > 0) {
        name += " (" + std::to_string(preview.source_width) + "x" +
                std::to_string(preview.source_height) + ")";
    }
    return name;
}

void draw_texture_choice(const char* label, int& selected) {
    ImGui::PushID(label);
    const std::string current = texture_choice_name(selected);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##texture", current.c_str())) {
        if (ImGui::Selectable("None", selected < 0)) selected = -1;
        for (size_t image = 0; image < s_texture_previews.size(); ++image) {
            const auto& preview = s_texture_previews[image];
            const bool active = selected == int(image);
            ImGui::PushID(int(image));
            const bool chosen = ImGui::Selectable(
                "##choice", active, ImGuiSelectableFlags_None,
                ImVec2(0.0f, 52.0f));
            const ImVec2 minimum = ImGui::GetItemRectMin();
            const ImVec2 maximum = ImGui::GetItemRectMax();
            ImDrawList* draw = ImGui::GetWindowDrawList();
            draw->PushClipRect(minimum, maximum, true);
            const float box = 44.0f;
            ImVec2 image_size(box, box);
            if (preview.preview_width > 0 && preview.preview_height > 0) {
                const float scale = std::min(
                    box / float(preview.preview_width),
                    box / float(preview.preview_height));
                image_size = ImVec2(float(preview.preview_width) * scale,
                                    float(preview.preview_height) * scale);
            }
            const ImVec2 image_min(
                minimum.x + 4.0f,
                minimum.y + (52.0f - image_size.y) * 0.5f);
            const ImVec2 image_max(image_min.x + image_size.x,
                                   image_min.y + image_size.y);
            const ImTextureID texture = preview_texture_id(preview);
            if (texture) {
                draw->AddImage(texture, image_min, image_max);
            } else {
                draw->AddRectFilled(
                    image_min, ImVec2(image_min.x + box, image_min.y + box),
                    ImGui::GetColorU32(ImGuiCol_FrameBg));
            }
            const float text_x = minimum.x + box + 12.0f;
            const std::string name = preview.name.empty()
                                         ? "image_" + std::to_string(image)
                                         : preview.name;
            draw->AddText(ImVec2(text_x, minimum.y + 6.0f),
                          ImGui::GetColorU32(ImGuiCol_Text), name.c_str());
            const std::string detail = preview.error.empty()
                ? std::to_string(preview.source_width) + "x" +
                      std::to_string(preview.source_height)
                : "Preview unavailable";
            draw->AddText(
                ImVec2(text_x, minimum.y + 28.0f),
                preview.error.empty()
                    ? ImGui::GetColorU32(ImGuiCol_TextDisabled)
                    : ImGui::GetColorU32(ImVec4(0.95f, 0.42f, 0.38f, 1.0f)),
                detail.c_str());
            draw->PopClipRect();
            if (active) ImGui::SetItemDefaultFocus();
            ImGui::PopID();
            if (chosen) selected = int(image);
        }
        ImGui::EndCombo();
    }
    ImGui::PopID();
}

void reset_material_assignment(size_t index) {
    if (index >= s_glb_scene.materials.size() ||
        index >= s_material_textures.size()) {
        return;
    }
    const auto& material = s_glb_scene.materials[index];
    auto& assignment = s_material_textures[index];
    assignment.diffuse = material.base_color;
    assignment.normal = material.normal;
    assignment.specular = material.occlusion;
    assignment.metallic = material.metallic_rough;
    assignment.extra = material.emissive;
}

bool selected_textures_valid() {
    if (!s_glb_error.empty()) return false;
    for (const auto& material : s_material_textures) {
        for (int image : {material.diffuse, material.normal,
                          material.specular, material.metallic,
                          material.extra}) {
            if (image < 0) continue;
            if (size_t(image) >= s_texture_previews.size() ||
                !s_texture_previews[image].error.empty()) {
                return false;
            }
        }
    }
    return true;
}

void draw_material_mapping_panel() {
    ImGui::TextUnformatted("Game material mapping");
    ImGui::Separator();
    if (!s_glb_error.empty()) {
        ImGui::TextColored(ImVec4(0.95f, 0.42f, 0.38f, 1.0f),
                           "%s", s_glb_error.c_str());
        return;
    }
    if (s_glb_scene.materials.empty()) {
        ImGui::TextDisabled("This model has no material records.");
        return;
    }

    s_selected_material = std::clamp(
        s_selected_material, 0, int(s_glb_scene.materials.size()) - 1);
    const std::string& current_name =
        s_glb_scene.materials[s_selected_material].name;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Material:");
    ImGui::SameLine(132.0f);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##material", current_name.c_str())) {
        for (size_t material = 0; material < s_glb_scene.materials.size();
             ++material) {
            const auto& source = s_glb_scene.materials[material];
            const bool selected = s_selected_material == int(material);
            ImGui::PushID(int(material));
            const bool chosen = ImGui::Selectable(source.name.c_str(), selected);
            if (selected) ImGui::SetItemDefaultFocus();
            ImGui::PopID();
            if (chosen) {
                s_selected_material = int(material);
            }
        }
        ImGui::EndCombo();
    }

    auto& assignment = s_material_textures[s_selected_material];
    if (ImGui::BeginTable("##material_texture_slots", 2,
                          ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Role", ImGuiTableColumnFlags_WidthFixed,
                                124.0f);
        ImGui::TableSetupColumn("Texture",
                                ImGuiTableColumnFlags_WidthStretch);
        auto slot = [&](const char* role, const char* id, int& texture) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s:", role);
            ImGui::TableSetColumnIndex(1);
            draw_texture_choice(id, texture);
        };
        slot("Diffuse / color", "diffuse", assignment.diffuse);
        slot("Normal", "normal", assignment.normal);
        slot("Specular", "specular", assignment.specular);
        slot("Metallic", "metallic", assignment.metallic);
        slot("Extra / emissive", "extra", assignment.extra);
        ImGui::EndTable();
    }

    if (ImGui::Button("Reset this material to source defaults")) {
        reset_material_assignment(size_t(s_selected_material));
    }
}

#ifdef _WIN32
void draw_settings_modal(ID3D11Device* device) {
#else
void draw_settings_modal() {
#endif
    if (s_open_settings) {
        ImGui::OpenPopup("Import Settings");
        s_open_settings = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    const bool show_glb_materials = s_mode == Mode::Glb;
    if (show_glb_materials) {
        const ImVec2 work = ImGui::GetMainViewport()->WorkSize;
        ImGui::SetNextWindowSize(
            ImVec2(std::min(720.0f, work.x * 0.92f),
                   std::min(760.0f, work.y * 0.92f)),
            ImGuiCond_Appearing);
    } else {
        ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_Appearing);
    }
    ImGuiWindowFlags popup_flags = ImGuiWindowFlags_NoSavedSettings;
    if (!show_glb_materials) popup_flags |= ImGuiWindowFlags_AlwaysAutoResize;
    if (!ImGui::BeginPopupModal("Import Settings", nullptr,
                                popup_flags)) {
        return;
    }

#ifdef _WIN32
    if (show_glb_materials) ensure_texture_previews(device);
#else
    if (show_glb_materials) ensure_texture_previews();
#endif

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
            "Every .glb/.obj and image directly inside the folder is "
            "imported (textures an .obj uses are baked into its model).");
    }

    ImGui::Combo("Texture format", &s_format_idx, kFormatNames,
                 IM_ARRAYSIZE(kFormatNames));
    const char* dim_names[] = {"256", "512", "1024", "2048", "4096"};
    ImGui::Combo("Max texture size", &s_max_dim_idx, dim_names,
                 IM_ARRAYSIZE(dim_names));
    ImGui::Checkbox("Generate mip chain (down to 16px, like retail)",
                    &s_gen_mips);

    if (show_glb_materials) {
        ImGui::Separator();
        ImGui::BeginChild("##glb_material_mapping", ImVec2(0, 310.0f), false);
        draw_material_mapping_panel();
        ImGui::EndChild();
    }

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
                    "Each imported model gets a PROP_<name> entity.");
            }
        }
    }

    ImGui::Separator();
    const bool can_import =
        !s_busy.load() && entity_id_ok &&
        (!show_glb_materials || selected_textures_valid()) &&
        (s_mode == Mode::Folder || s_mode == Mode::TextureReplace ||
         !s_asset_name.empty());
    if (!can_import) ImGui::BeginDisabled();
    if (ImGui::Button(s_mode == Mode::TextureReplace ? "Replace" : "Import",
                      ImVec2(120, 0))) {
        launch_worker();
        clear_glb_editor();
        ImGui::CloseCurrentPopup();
    }
    if (!can_import) ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
        clear_glb_editor();
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

#ifdef _WIN32
void Draw(ID3D11Device* device) {
#else
void Draw() {
#endif
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
                clear_glb_editor();
                s_mode = Mode::None;
            }
            ImGuiFileDialog::Instance()->Close();
        }
    };
    handle_picker("ImportPickGlb");
    handle_picker("ImportPickImage");
    handle_picker("ImportPickFolder");
    handle_picker("ReplaceTexturePickImage");

#ifdef _WIN32
    draw_settings_modal(device);
#else
    draw_settings_modal();
#endif
    poll_worker();
    tree_apply_pending_injections();
}

}
