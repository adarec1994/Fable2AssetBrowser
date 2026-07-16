#include "TerrainTextureRegistry.h"

#include <unordered_map>

namespace TerrainTextureRegistry {

namespace {

std::unordered_map<std::string, Entry>& storage()
{
    static std::unordered_map<std::string, Entry> s;
    return s;
}

std::vector<LodPaletteEntry>& lod_storage()
{
    static std::vector<LodPaletteEntry> v;
    return v;
}

}

void Register(const std::string&     name,
              std::vector<uint8_t>   rgba,
              int                    width,
              int                    height)
{
    Entry e;
    e.rgba   = std::move(rgba);
    e.width  = width;
    e.height = height;
    storage()[name] = std::move(e);
}

const Entry* Find(const std::string& name)
{
    auto& s = storage();
    auto it = s.find(name);
    return (it == s.end()) ? nullptr : &it->second;
}

void Clear()
{
    storage().clear();
    lod_storage().clear();
}

void SetLodPalette(std::vector<LodPaletteEntry> entries)
{
    lod_storage() = std::move(entries);
}

const std::vector<LodPaletteEntry>& GetLodPalette()
{
    return lod_storage();
}

}
