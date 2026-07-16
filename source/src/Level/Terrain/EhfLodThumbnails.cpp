#include "EhfLodThumbnails.h"

#ifdef _WIN32
#include <d3d11.h>
#endif

namespace EhfLodThumbnails {

namespace {

std::vector<Entry>& storage()
{
    static std::vector<Entry> v;
    return v;
}

#ifdef _WIN32
void release_one(ID3D11ShaderResourceView*& srv) {
    if (srv) { srv->Release(); srv = nullptr; }
}
#endif

void release_all(std::vector<Entry>& v) {
#ifdef _WIN32
    for (auto& e : v) {
        release_one(e.srv_base_diffuse);
        release_one(e.srv_base_normal);
        release_one(e.srv_detail_diffuse);
        release_one(e.srv_detail_normal);
    }
#endif
}

}

void Set(std::vector<Entry> entries)
{
    auto& dst = storage();
    release_all(dst);
    dst = std::move(entries);
}

const std::vector<Entry>& Get()
{
    return storage();
}

void Clear()
{
    auto& v = storage();
    release_all(v);
    v.clear();
}

}
