#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Level {
struct PropBlock;
}

namespace FoliageEdit {

struct Instance {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float yaw = 0.0f;
    float scale = 1.0f;
};

void Clear();
bool Empty();
bool Dirty();
void SetClean();
bool ShouldBake();
uint64_t Generation();

void Add(const std::string& model_path, const Instance& inst);
size_t EraseAllInRadius(float x, float y, float radius,
                        std::vector<std::string>* touched_models);
size_t EraseModelsInRadius(const std::vector<std::string>& models,
                           float x, float y, float radius,
                           std::vector<std::string>* touched_models);
bool HasInstanceWithin(const std::string& model_path, float x, float y,
                       float radius);

void Models(std::vector<std::string>& out);
std::vector<Instance> Snapshot(const std::string& model_path);
std::vector<Instance> SnapshotRect(const std::string& model_path,
                                   float min_x, float min_y,
                                   float max_x, float max_y);
size_t TotalInstances();
size_t InstanceCount(const std::string& model_path);

void PopulateFromParsedBlocks(const std::vector<Level::PropBlock>& blocks);
bool RewriteEngineLevelBytes(std::vector<uint8_t>& lev_bytes,
                             std::string& err);

}
