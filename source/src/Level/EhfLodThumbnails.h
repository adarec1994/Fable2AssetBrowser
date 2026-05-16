#pragma once

#include <string>
#include <vector>


#ifdef _WIN32
struct ID3D11ShaderResourceView;
#endif

namespace EhfLodThumbnails {

struct Entry {
#ifdef _WIN32
    ID3D11ShaderResourceView* srv_base_diffuse   = nullptr;
    ID3D11ShaderResourceView* srv_base_normal    = nullptr;
    ID3D11ShaderResourceView* srv_detail_diffuse = nullptr;
    ID3D11ShaderResourceView* srv_detail_normal  = nullptr;
#endif
    std::string base_diffuse_path;
    std::string base_normal_path;
    std::string detail_diffuse_path;
    std::string detail_normal_path;

    int base_diffuse_w = 0,   base_diffuse_h = 0;
    int base_normal_w = 0,    base_normal_h = 0;
    int detail_diffuse_w = 0, detail_diffuse_h = 0;
    int detail_normal_w = 0,  detail_normal_h = 0;
};

void Set(std::vector<Entry> entries);

const std::vector<Entry>& Get();

void Clear();

}  
