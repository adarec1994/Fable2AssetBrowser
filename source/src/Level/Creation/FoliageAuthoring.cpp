#include "FoliageAuthoring.h"

#include "Level/Core/LevelLoader.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>
#include <tuple>

namespace FoliageEdit {

namespace {

constexpr size_t kHeaderSize = 17 + 4 + 4;
constexpr size_t kCountOffset = 17 + 4;
constexpr float kClusterCell = 24.0f;
constexpr size_t kMaxBlockInstances = 240;
constexpr uint32_t kBlockConstants[7] = {
    0x3F000000u, 0x3F800000u, 0x42000000u, 0x00000000u,
    0x42000000u, 0x3CCCCCCDu, 0x41800000u,
};

std::mutex g_mutex;
std::map<std::string, std::vector<Instance>> g_store;
bool g_dirty = false;
uint64_t g_generation = 1;

void put_u8(std::vector<uint8_t>& out, uint8_t v) { out.push_back(v); }

void put_u32(std::vector<uint8_t>& out, uint32_t v)
{
    out.push_back((uint8_t)(v >> 24));
    out.push_back((uint8_t)(v >> 16));
    out.push_back((uint8_t)(v >> 8));
    out.push_back((uint8_t)v);
}

void put_u64(std::vector<uint8_t>& out, uint64_t v)
{
    put_u32(out, (uint32_t)(v >> 32));
    put_u32(out, (uint32_t)v);
}

void put_f32(std::vector<uint8_t>& out, float f)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &f, 4);
    put_u32(out, bits);
}

uint16_t f32_to_half(float f)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &f, 4);
    const uint32_t sign = (bits >> 16) & 0x8000u;
    const int32_t exp = (int32_t)((bits >> 23) & 0xFF) - 127 + 15;
    const uint32_t mant = bits & 0x7FFFFFu;
    if (exp <= 0) return (uint16_t)sign;
    if (exp >= 31) return (uint16_t)(sign | 0x7C00u);
    return (uint16_t)(sign | ((uint32_t)exp << 10) | (mant >> 13));
}

void put_half(std::vector<uint8_t>& out, float f)
{
    const uint16_t h = f32_to_half(f);
    out.push_back((uint8_t)(h >> 8));
    out.push_back((uint8_t)h);
}

void put_cstr(std::vector<uint8_t>& out, const std::string& s)
{
    out.insert(out.end(), s.begin(), s.end());
    out.push_back(0);
}

uint64_t block_id(const std::string& model, size_t index,
                  const Instance& first)
{
    uint64_t h = 14695981039346656037ull;
    auto mix = [&](const void* data, size_t n) {
        const uint8_t* p = (const uint8_t*)data;
        for (size_t i = 0; i < n; ++i) {
            h = (h ^ p[i]) * 1099511628211ull;
        }
    };
    mix(model.data(), model.size());
    mix(&index, sizeof(index));
    mix(&first, sizeof(first));
    return h;
}

void serialize_block(std::vector<uint8_t>& out, const std::string& model,
                     size_t index, const std::vector<const Instance*>& group)
{
    put_u32(out, 21u);
    put_cstr(out, model);
    put_u8(out, 0);
    put_u64(out, block_id(model, index, *group.front()));
    put_u8(out, 0);
    put_u8(out, 0);
    put_u32(out, (uint32_t)group.size());
    for (uint32_t c : kBlockConstants) put_u32(out, c);

    float cx = 0.0f, cy = 0.0f, cz = 0.0f;
    for (const Instance* inst : group) {
        cx += inst->x;
        cy += inst->y;
        cz += inst->z;
    }
    const float inv = 1.0f / (float)group.size();
    cx *= inv;
    cy *= inv;
    cz *= inv;
    float radius = 0.0f;
    for (const Instance* inst : group) {
        const float dx = inst->x - cx;
        const float dy = inst->y - cy;
        const float dz = inst->z - cz;
        radius = std::max(radius,
                          std::sqrt(dx * dx + dy * dy + dz * dz));
    }
    put_f32(out, cx);
    put_f32(out, cy);
    put_f32(out, cz);
    put_f32(out, radius + 4.0f);
    for (int i = 0; i < 6; ++i) put_u32(out, 0);

    for (const Instance* inst : group) {
        put_f32(out, inst->x);
        put_f32(out, inst->y);
        put_f32(out, inst->z);
        put_half(out, 0.0f);
        put_half(out, 0.0f);
        put_half(out, std::sin(inst->yaw * 0.5f));
        put_half(out, std::cos(inst->yaw * 0.5f));
        put_half(out, inst->scale > 0.0f ? inst->scale : 1.0f);
    }
    put_u32(out, 0);
}

}

void Clear()
{
    std::lock_guard<std::mutex> lk(g_mutex);
    g_store.clear();
    g_dirty = false;
    ++g_generation;
}

uint64_t Generation()
{
    std::lock_guard<std::mutex> lk(g_mutex);
    return g_generation;
}

bool Empty()
{
    std::lock_guard<std::mutex> lk(g_mutex);
    for (const auto& [model, list] : g_store) {
        if (!list.empty()) return false;
    }
    return true;
}

bool Dirty()
{
    std::lock_guard<std::mutex> lk(g_mutex);
    return g_dirty;
}

void SetClean()
{
    std::lock_guard<std::mutex> lk(g_mutex);
    g_dirty = false;
}

bool ShouldBake() { return Dirty(); }

void Add(const std::string& model_path, const Instance& inst)
{
    if (model_path.empty()) return;
    std::lock_guard<std::mutex> lk(g_mutex);
    g_store[model_path].push_back(inst);
    g_dirty = true;
}

size_t EraseAllInRadius(float x, float y, float radius,
                        std::vector<std::string>* touched_models)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    const float r2 = radius * radius;
    size_t removed = 0;
    for (auto& [model, list] : g_store) {
        const size_t before = list.size();
        list.erase(std::remove_if(list.begin(), list.end(),
                                  [&](const Instance& inst) {
                       const float dx = inst.x - x;
                       const float dy = inst.y - y;
                       return dx * dx + dy * dy <= r2;
                   }),
                   list.end());
        if (list.size() != before) {
            removed += before - list.size();
            if (touched_models) touched_models->push_back(model);
        }
    }
    if (removed) g_dirty = true;
    return removed;
}

size_t EraseModelsInRadius(const std::vector<std::string>& models,
                           float x, float y, float radius,
                           std::vector<std::string>* touched_models)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    const float r2 = radius * radius;
    size_t removed = 0;
    for (const std::string& model : models) {
        const auto it = g_store.find(model);
        if (it == g_store.end()) continue;
        auto& list = it->second;
        const size_t before = list.size();
        list.erase(std::remove_if(list.begin(), list.end(),
                                  [&](const Instance& inst) {
                       const float dx = inst.x - x;
                       const float dy = inst.y - y;
                       return dx * dx + dy * dy <= r2;
                   }),
                   list.end());
        if (list.size() != before) {
            removed += before - list.size();
            if (touched_models) touched_models->push_back(model);
        }
    }
    if (removed) g_dirty = true;
    return removed;
}

bool HasInstanceWithin(const std::string& model_path, float x, float y,
                       float radius)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    const auto it = g_store.find(model_path);
    if (it == g_store.end()) return false;
    const float r2 = radius * radius;
    for (const Instance& inst : it->second) {
        const float dx = inst.x - x;
        const float dy = inst.y - y;
        if (dx * dx + dy * dy <= r2) return true;
    }
    return false;
}

void Models(std::vector<std::string>& out)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    out.clear();
    for (const auto& [model, list] : g_store) {
        if (!list.empty()) out.push_back(model);
    }
}

std::vector<Instance> Snapshot(const std::string& model_path)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    const auto it = g_store.find(model_path);
    return it == g_store.end() ? std::vector<Instance>{} : it->second;
}

std::vector<Instance> SnapshotRect(const std::string& model_path,
                                   float min_x, float min_y,
                                   float max_x, float max_y)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    std::vector<Instance> out;
    const auto it = g_store.find(model_path);
    if (it == g_store.end()) return out;
    for (const Instance& inst : it->second) {
        if (inst.x >= min_x && inst.x < max_x &&
            inst.y >= min_y && inst.y < max_y) {
            out.push_back(inst);
        }
    }
    return out;
}

size_t TotalInstances()
{
    std::lock_guard<std::mutex> lk(g_mutex);
    size_t n = 0;
    for (const auto& [model, list] : g_store) n += list.size();
    return n;
}

size_t InstanceCount(const std::string& model_path)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    const auto it = g_store.find(model_path);
    return it == g_store.end() ? 0 : it->second.size();
}

void PopulateFromParsedBlocks(const std::vector<Level::PropBlock>& blocks)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    g_store.clear();
    g_dirty = false;
    ++g_generation;
    for (const Level::PropBlock& block : blocks) {
        if (block.type != 21 || block.model_path.empty()) continue;
        auto& list = g_store[block.model_path];
        list.reserve(list.size() + block.instances.size());
        for (const Level::PropInstance& inst : block.instances) {
            Instance out;
            out.x = inst.values[0];
            out.y = inst.values[1];
            out.z = inst.values[2];
            out.yaw = std::atan2(inst.values[6], inst.values[7]);
            out.scale = inst.values[9] > 0.0f ? inst.values[9] : 1.0f;
            list.push_back(out);
        }
    }
}

bool RewriteEngineLevelBytes(std::vector<uint8_t>& lev_bytes,
                             std::string& err)
{
    Level::EngineLevelInfo info;
    if (!Level::ParseEngineLevel(lev_bytes, info) || !info.ok) {
        err = "foliage bake: engine_level parse failed: " + info.error;
        return false;
    }
    if (info.entries.size() != info.entry_count) {
        err = "foliage bake: level has entries the parser does not "
              "understand; aborting to avoid corrupting it";
        return false;
    }
    if (lev_bytes.size() < kHeaderSize) {
        err = "foliage bake: engine_level too small";
        return false;
    }

    std::vector<uint8_t> out;
    out.reserve(lev_bytes.size() + TotalInstances() * 22 + 4096);
    out.insert(out.end(), lev_bytes.begin(),
               lev_bytes.begin() + kHeaderSize);

    uint32_t entry_count = 0;
    for (const Level::EngineLevelEntry& e : info.entries) {
        if (e.type == 21) continue;
        if (e.offset + e.size > lev_bytes.size() || e.size == 0) {
            err = "foliage bake: bad entry span";
            return false;
        }
        out.insert(out.end(), lev_bytes.begin() + e.offset,
                   lev_bytes.begin() + e.offset + e.size);
        ++entry_count;
    }

    std::lock_guard<std::mutex> lk(g_mutex);
    for (const auto& [model, list] : g_store) {
        if (list.empty()) continue;
        std::map<std::pair<int, int>, std::vector<const Instance*>> cells;
        for (const Instance& inst : list) {
            const int cx = (int)std::floor(inst.x / kClusterCell);
            const int cy = (int)std::floor(inst.y / kClusterCell);
            cells[{cx, cy}].push_back(&inst);
        }
        size_t block_index = 0;
        for (auto& [cell, members] : cells) {
            for (size_t start = 0; start < members.size();
                 start += kMaxBlockInstances) {
                const size_t end = std::min(
                    members.size(), start + kMaxBlockInstances);
                std::vector<const Instance*> group(
                    members.begin() + start, members.begin() + end);
                serialize_block(out, model, block_index++, group);
                ++entry_count;
            }
        }
    }

    out[kCountOffset + 0] = (uint8_t)(entry_count >> 24);
    out[kCountOffset + 1] = (uint8_t)(entry_count >> 16);
    out[kCountOffset + 2] = (uint8_t)(entry_count >> 8);
    out[kCountOffset + 3] = (uint8_t)entry_count;

    lev_bytes = std::move(out);
    return true;
}

}
