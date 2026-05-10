#pragma once

#include <string>
#include <vector>
#include <cstdint>

enum class TexExportFormat {
    PNG,
    JPG,
    TIFF,
    DDS,

    TEX,
};

const char* tex_export_extension(TexExportFormat fmt);

bool tex_export_png (const std::string& path, const uint8_t* rgba, int w, int h);
bool tex_export_jpg (const std::string& path, const uint8_t* rgba, int w, int h);
bool tex_export_tiff(const std::string& path, const uint8_t* rgba, int w, int h);
bool tex_export_dds (const std::string& path, const uint8_t* rgba, int w, int h);

bool tex_export_rgba(const std::string& path, TexExportFormat fmt,
                     const uint8_t* rgba, int w, int h);

void tex_export_begin_rgba(TexExportFormat fmt,
                           const std::string& base_name,
                           std::vector<uint8_t> rgba, int w, int h);

void tex_export_begin_blob(TexExportFormat fmt,
                           const std::string& base_name,
                           std::vector<unsigned char> blob,
                           int mip_index);

void tex_export_begin_named(TexExportFormat fmt,
                            const std::string& tex_name,
                            const std::string& preferred_bnk,
                            int mip_index);

void tex_export_drive();

void tex_export_menu_rgba (const std::string& base_name,
                           const std::vector<uint8_t>& rgba, int w, int h);
void tex_export_menu_blob (const std::string& base_name,
                           const std::vector<unsigned char>& blob,
                           int mip_index);
void tex_export_menu_named(const std::string& base_name,
                           const std::string& tex_name,
                           const std::string& preferred_bnk,
                           int mip_index);
