#include "VfsConfigViewer.h"

#include "ContentTabs.h"
#include "OutputLog.h"
#include "../BNKCore.cpp"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace VfsConfigViewer {
namespace {

constexpr std::size_t kMaxPreviewBytes = 2u * 1024u * 1024u;

std::string leaf_name(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

}

bool IsFileName(const std::string& name) {
    std::string lower = leaf_name(name);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    constexpr const char* suffix = ".vfsconfig";
    constexpr std::size_t suffix_size = 10;
    return lower.size() >= suffix_size &&
           lower.compare(lower.size() - suffix_size, suffix_size, suffix) ==
               0;
}

bool OpenBnkEntry(const std::string& bnk_path, int file_index,
                  const std::string& virtual_path) {
    if (bnk_path.empty() || file_index < 0) {
        OutputLog::error("VFS config viewer: invalid bank entry");
        return false;
    }

    try {
        std::vector<std::uint8_t> bytes =
            BnkCache::extract_bytes(bnk_path, file_index);
        if (bytes.size() > kMaxPreviewBytes) {
            OutputLog::error(
                "VFS config viewer: file is too large to preview (>2 MB)");
            return false;
        }

        std::size_t begin = 0;
        if (bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb &&
            bytes[2] == 0xbf) {
            begin = 3;
        }
        std::string content(bytes.begin() + static_cast<std::ptrdiff_t>(begin),
                            bytes.end());
        while (!content.empty() && content.back() == '\0') {
            content.pop_back();
        }

        const std::string key = bnk_path + "::" + virtual_path + "#" +
                                std::to_string(file_index);
        std::string title = leaf_name(virtual_path);
        if (title.empty()) title = "level.vfsconfig";
        ContentTabs::OpenVfsConfig(key, title, content);
        OutputLog::info("Viewing VFS config: " + virtual_path);
        return true;
    } catch (const std::exception& ex) {
        OutputLog::error("VFS config viewer: " + std::string(ex.what()));
    } catch (...) {
        OutputLog::error("VFS config viewer: failed to extract bank entry");
    }
    return false;
}

void Draw(const std::string* content) {
    const std::string empty;
    const std::string& text = content ? *content : empty;

    ImGui::TextDisabled("Read-only VFS configuration");
    ImVec2 size = ImGui::GetContentRegionAvail();
    ImGui::PushStyleColor(ImGuiCol_FrameBg,
                          ImVec4(0.08f, 0.08f, 0.10f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,
                          ImVec4(0.10f, 0.10f, 0.12f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,
                          ImVec4(0.10f, 0.10f, 0.12f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text,
                          ImVec4(0.85f, 0.92f, 0.82f, 1.0f));
    ImGui::InputTextMultiline(
        "##vfsconfig_text", const_cast<char*>(text.c_str()), text.size() + 1,
        size, ImGuiInputTextFlags_ReadOnly);
    ImGui::PopStyleColor(4);
}

}
