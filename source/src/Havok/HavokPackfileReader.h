#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Havok {

struct SectionHeader {
    std::string name;
    uint32_t    absolute_data_start = 0;
    uint32_t    local_fixups        = 0;
    uint32_t    global_fixups       = 0;
    uint32_t    virtual_fixups      = 0;
    uint32_t    exports             = 0;
    uint32_t    imports             = 0;
    uint32_t    end_offset          = 0;
};

struct ClassEntry {
    uint32_t    hash = 0;
    std::string name;
    uint32_t    classnames_offset = 0;
};

struct VirtualFixup {
    uint32_t classnames_offset = 0;
    uint32_t data_offset       = 0;
    uint32_t section_idx       = 0;
};

struct PackFile {
    std::vector<uint8_t> bytes;

    std::string version_string;

    SectionHeader classnames_section;
    SectionHeader data_section;
    SectionHeader types_section;

    std::vector<ClassEntry> classes;

    std::vector<VirtualFixup> virtual_fixups;

    const ClassEntry* find_class(const std::string& name) const;

    const ClassEntry* class_for(uint32_t classnames_offset) const;
};

std::optional<PackFile> LoadPackFile(const std::string& path);

std::optional<PackFile> LoadPackFileFromBytes(std::vector<uint8_t> bytes,
                                              const std::string& source_label);

void LogPackFileSummary(const PackFile& pf);

void LogRigidBodyBytes(const PackFile& pf,
                       size_t max_bodies = 3,
                       float world_max_xy = 400.0f,
                       float world_max_z  = 200.0f);

void LogClassInstanceBytes(const PackFile& pf,
                           const std::string& class_name,
                           size_t max_instances = 3,
                           size_t dump_size = 0x200,
                           float world_max_xy = 400.0f,
                           float world_max_z  = 200.0f);

void ScanDataSection(const PackFile& pf,
                     float world_max_xy = 400.0f,
                     float world_max_z  = 200.0f,
                     size_t max_strings = 200,
                     size_t max_vec3s   = 200);

struct CandidatePlacement {
    float x, y, z;
};
std::vector<CandidatePlacement>
ExtractCandidatePlacements(const PackFile& pf,
                           float world_max_xy = 500.0f,
                           float world_max_z  = 200.0f);

struct LocalFixup {
    uint32_t src;
    uint32_t dst;
};

struct CollisionMesh {
    std::vector<float>    vertices;
    std::vector<uint16_t> indices16;
    std::vector<uint32_t> indices32;
};

void ApplyLocalFixups(PackFile& pf);

std::vector<CollisionMesh> ExtractCollisionMeshes(const PackFile& pf);

}
