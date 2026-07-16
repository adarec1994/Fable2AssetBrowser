#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Level {

struct VistaPatchGeom;
struct TerrainMesh;

bool BuildEhfRenderStripMesh(
    const std::vector<uint8_t>& ehf,
    TerrainMesh& out,
    std::string* out_stats = nullptr);

bool BuildEhfVistaPatchMesh(
    const std::vector<uint8_t>& ehf,
    TerrainMesh& out,
    std::string* out_stats = nullptr);

bool BuildEhfVistaPatchGeoms(
    const std::vector<uint8_t>& ehf,
    std::vector<VistaPatchGeom>& out_geoms,
    std::string* out_stats = nullptr);

}
