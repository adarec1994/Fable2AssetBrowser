#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "Skybox/EnvironmentThemeParser.h"

namespace Gdb {

struct Placement {
    float    x, y, z;
    float    yaw;
    float    rot_x, rot_y, rot_z;
    float    scale;
    bool     has_rotation;
    uint32_t pos_value_off[3] = {0, 0, 0};
    uint32_t marker;
    uint32_t hash_a;
    uint32_t parent_hash;
    uint32_t model_path_hash;
    uint32_t skeleton_file_hash;
    uint32_t retarget_skeleton_file_hash;
    std::vector<uint32_t> model_path_hashes;
    bool     indexed_record;
    bool     transform_from_indexed_record;
    std::string entity_name;
};

struct GdbInfo {
    std::vector<Placement> placements;
};

struct RecordRow {
    uint32_t index = 0;
    uint32_t hash = 0;
    uint32_t parent_hash = 0;
    uint32_t model_path_hash = 0;
    uint32_t skeleton_file_hash = 0;
    uint32_t retarget_skeleton_file_hash = 0;
    std::vector<uint32_t> model_path_hashes;
    std::string name;
    std::string skeleton_file_name;
    std::string retarget_skeleton_file_name;
};

GdbInfo Parse(const std::vector<uint8_t>& bytes);

GdbInfo ParseWithSaveMap(
    const std::vector<uint8_t>& bytes,
    const std::vector<std::pair<uint32_t, std::string>>& hash_to_name);

bool LookupModelPathHash(
    const std::vector<uint8_t>& bytes,
    uint32_t record_hash,
    uint32_t& out_model_path_hash,
    uint32_t* out_parent_hash = nullptr);

bool LookupModelPathHashes(
    const std::vector<uint8_t>& bytes,
    uint32_t record_hash,
    std::vector<uint32_t>& out_model_path_hashes,
    uint32_t* out_parent_hash = nullptr);

bool LookupPlacement(
    const std::vector<uint8_t>& bytes,
    uint32_t record_hash,
    const std::string& entity_name,
    Placement& out_placement);

std::vector<RecordRow> Build010RecordRows(
    const std::vector<uint8_t>& bytes,
    const std::vector<std::pair<uint32_t, std::string>>& hash_to_name);

// A particle effect bound to an object template via the GDB ParticleEffect
// (0x5B009F68) field chain. fx_hash_lower = FNV1 of the LOWERCASED effect
// name — the key the particle bank's trailing effect map uses.
struct ParticleFxBinding {
    uint32_t fx_hash_lower = 0;
    std::string fx_name;
};

// A particle effect spawned by a ParticleAttacher component (DummyObject,
// ParticleEmitter, OrientParticleToAttachmentPoint, OffsetFromDummy,
// MaxVisibilityDistance...). Keyed by the entity/template record.
struct ParticleAttachmentBinding {
    uint32_t fx_hash_lower = 0;
    std::string fx_name;
    uint32_t component_hash = 0;
    uint32_t emitter_record_hash = 0;
    uint32_t dummy_hash = 0;
    std::string dummy_name;
    float offset[3] = {0.0f, 0.0f, 0.0f};
    float max_visibility_distance = 0.0f;
    bool override_max_visibility_distance = false;
    bool disable_when_parent_invisible = false;
    bool orient_to_attachment_point = false;
};

std::unordered_map<uint32_t, std::vector<ParticleFxBinding>>
ExtractParticleFxBindings(
    const std::vector<const std::vector<uint8_t>*>& gdbs);

std::unordered_map<uint32_t, std::vector<ParticleAttachmentBinding>>
ExtractParticleAttachmentBindings(
    const std::vector<const std::vector<uint8_t>*>& gdbs);

// An authored FX entity from the level: a .save-named record (e.g.
// "FX_Water_Fall_Main_Wider") whose type chain carries the
// CParticleSystemEntityType effect hash (field 0x4EDC9083, Fnv1Lower of the
// bank effect name) and whose transform comes from the record's own
// component chain. These are the game's real level FX spawns — exact effect
// + exact position, no name heuristics.
struct FxEntityPlacement {
    std::string name;             // .save entity name
    uint32_t record_hash = 0;
    float x = 0, y = 0, z = 0;    // game space (z up)
    float rot_x = 0, rot_y = 0, rot_z = 0;
    bool  has_rotation = false;
    uint32_t fx_hash = 0;         // Fnv1Lower(effect name) — bank map key
};

std::vector<FxEntityPlacement> ExtractFxEntityPlacements(
    const std::vector<uint8_t>& level_bytes,
    const std::vector<const std::vector<uint8_t>*>& gdbs,
    const std::vector<std::pair<uint32_t, std::string>>& hash_to_name);

}
