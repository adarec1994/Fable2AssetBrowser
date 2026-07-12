#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <variant>
#include <vector>

namespace CloudRuntime {

constexpr std::size_t kLayerCount = 4;

struct LayerThemeValues {
    float transparency = 0.0f;
    float height = 0.0f;
    float normal_strength = 0.0f;
    float translucency_strength = 0.0f;
    float ambient_light = 0.0f;
    float brightness = 0.0f;
    float position_x = 0.0f;
    float position_y = 0.0f;
    float size_x = 0.0f;
    float size_y = 0.0f;
    float texture_scale_x = 0.001f;
    float texture_scale_y = 0.001f;
    float velocity_x = 0.0f;
    float velocity_y = 0.0f;
    std::uint32_t density_token = 0;
};

struct LayerConfig {
    float transparency;
    float height;
    float normal_strength;
    float translucency_strength;
    float alpha_reference;
    float ambient_light;
    float brightness;
    float position_x;
    float position_y;
    float size_x;
    float size_y;
    float texture_scale_x;
    float texture_scale_y;
    float velocity_x;
    float velocity_y;
    std::uint32_t density_token;
};

static_assert(sizeof(LayerConfig) == 0x40);
static_assert(offsetof(LayerConfig, alpha_reference) == 0x10);
static_assert(offsetof(LayerConfig, texture_scale_x) == 0x2c);
static_assert(offsetof(LayerConfig, velocity_x) == 0x34);
static_assert(offsetof(LayerConfig, density_token) == 0x3c);

struct LayerRuntimeXex {
    LayerConfig config;
    std::uint32_t density_resource;
    std::uint32_t secondary_resource;
    double last_update_time;
    float uv_x;
    float uv_y;
};

static_assert(sizeof(LayerRuntimeXex) == 0x58);
static_assert(offsetof(LayerRuntimeXex, density_resource) == 0x40);
static_assert(offsetof(LayerRuntimeXex, last_update_time) == 0x48);
static_assert(offsetof(LayerRuntimeXex, uv_x) == 0x50);

struct Vertex {
    float x;
    float y;
    float z;
    float u;
    float v;
};

static_assert(sizeof(Vertex) == 20);

struct ActiveOrder {
    std::array<std::uint8_t, kLayerCount> indices{};
    std::size_t count = 0;
};

struct DensityResourceBinding {
    std::uint32_t object_token = 0;
    std::uint32_t identity_token = 0;
};

enum class DensityResourceLifetimeEventKind : std::uint8_t {
    ResolveIdentityToken,
    ReleaseLayerReferenceMayDestroy,
    AssignLayerPointer,
    AddLayerReference,
    ReleaseResolverTemporaryMayDestroy,
};

struct DensityResourceLifetimeEvent {
    DensityResourceLifetimeEventKind kind =
        DensityResourceLifetimeEventKind::ResolveIdentityToken;
    std::uint32_t object_token = 0;
    std::uint32_t identity_token = 0;
};

struct DensityCandidate {
    float weight = 0.0f;
    std::uint32_t token = 0;
};

struct Float4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;
};

enum class ConstantKind : std::uint8_t {
    Scalar,
    Float2,
    Vector,
};

struct ConstantSetterCall {
    std::uint32_t logical_id = 0;
    ConstantKind kind = ConstantKind::Scalar;
    Float4 value{};
    bool has_shader_binding = false;
};

struct RenderStateWrite {
    std::uint32_t state_id = 0;
    std::uint32_t value = 0;
};

struct InheritedStateRequirement {
    std::uint32_t state_id = 0;
    bool value_known_on_call_chain = false;
    std::uint32_t value = 0;
};

struct SamplerStateWrite {
    std::uint32_t hardware_slot = 0;
    std::uint32_t state_id = 0;
    std::uint32_t value = 0;
};

struct InheritedSamplerStateRequirement {
    std::uint32_t hardware_slot = 0;
    std::uint32_t state_index = 0;
    bool initial_device_value_known = false;
    std::uint32_t initial_device_value = 0;
};

struct InheritedPixelConstantRequirement {
    std::uint32_t register_index = 0;
    std::uint8_t component = 0;
    bool consumed_by_selected_shader = false;
    bool value_known_on_call_chain = false;
    float value = 0.0f;
};

struct TextureFetchControls {
    bool valid = false;
    std::uint32_t hardware_slot = 0;
    bool normalized_coordinates = false;
    std::uint8_t mag_filter = 0;
    std::uint8_t min_filter = 0;
    std::uint8_t mip_filter = 0;
    std::uint8_t aniso_filter = 0;
    std::uint8_t arbitrary_filter = 0;
    std::uint8_t volume_mag_filter = 0;
    std::uint8_t volume_min_filter = 0;
    bool use_computed_lod = false;
    std::uint8_t register_lod = 0;
};

struct ResourcePrepareCall {
    std::uint32_t resource_object = 0;
    std::uint32_t prepare_result_byte_offset = 0;
    std::uint32_t payload_field_byte_offset = 0;
    bool payload_known = false;
    std::uint32_t payload = 0;
    bool streaming_state_known = false;
    std::uint32_t streaming_state = 0;
};

enum class ResourceBindingPayloadSource : std::uint8_t {
    None,
    PreparedResultPlus4,
    GlobalDefault8331AF70,
};

struct ResourceBindingCall {
    std::uint32_t logical_id = 0;
    std::int32_t descriptor_slot = -1;
    bool has_shader_binding = false;
    ResourceBindingPayloadSource payload_source =
        ResourceBindingPayloadSource::None;
    std::uint32_t owner_object = 0;
    bool payload_known = false;
    std::uint32_t payload = 0;
    std::uint32_t bind_flags = 0;
    bool binder_called = false;
    std::uint32_t default_payload_address = 0;
};

struct PreparedResourceInput {
    bool present = false;
    std::uint32_t object = 0;
    bool prepare_result_plus4_payload_known = false;
    std::uint32_t prepare_result_plus4_payload = 0;
    bool object_plus54_state_known = false;
    std::uint32_t object_plus54_state = 0;
};

struct VertexElement {
    std::uint32_t stream = 0;
    std::uint32_t offset = 0;
    std::uint32_t type = 0;
    std::uint32_t usage = 0;
    std::uint32_t usage_index = 0;
};

enum class BackgroundMapAction : std::uint8_t {
    None,
    BindDefault,
    BindArgument,
};

struct BackgroundMapResourceBinding {
    std::uint32_t hardware_slot = 0;
    std::uint32_t owner_object = 0;
    std::uint32_t resource_field_offset = 0;
    std::uint32_t bind_flags = 0;
};

struct BackgroundMapSamplerMethodWrite {
    std::uint32_t hardware_slot = 0;

    std::uint32_t method_offset = 0;
    std::uint32_t value = 0;
};

using RawFloat4 = std::array<std::uint32_t, 4>;

enum class DirectConstantStage : std::uint8_t {
    Vertex,
    Pixel,
};

template <std::size_t RegisterCapacity>
struct ConditionalDirectConstantUpload {
    bool call_site_reached = false;
    DirectConstantStage stage = DirectConstantStage::Vertex;
    std::uint32_t first_register = 0;
    std::uint32_t register_count = 0;
    std::uint64_t dirty_mask = 0;
    bool guard_known = false;
    bool guard_enabled = false;
    bool payload_known = false;
    std::array<RawFloat4, RegisterCapacity> payload_words{};
};

using VertexC0C3DirectUpload = ConditionalDirectConstantUpload<4>;
using PixelC28DirectUpload = ConditionalDirectConstantUpload<1>;

enum class BackgroundMapVectorComponent : std::uint8_t {
    X,
    Y,
    Z,
    W,
};

struct BackgroundMapFloat4CopySource {
    std::uint32_t destination_register = 0;
    std::uint32_t object_byte_offset = 0;
};

struct BackgroundMapContextVectorPath {
    std::array<std::uint32_t, 2> pointer_offsets{};
    std::uint32_t vector_byte_offset = 0;
};

struct BackgroundMapPreviousOperationSource {
    std::uint8_t operation_index = 0;
};

struct BackgroundMapObjectWordSource {
    std::uint32_t byte_offset = 0;
};

struct BackgroundMapContextComponentSource {
    BackgroundMapVectorComponent component =
        BackgroundMapVectorComponent::X;
};

struct BackgroundMapImmediateSingleSource {
    std::uint32_t bits = 0;
};

using BackgroundMapPpcOperand = std::variant<
    std::monostate,
    BackgroundMapPreviousOperationSource,
    BackgroundMapObjectWordSource,
    BackgroundMapContextComponentSource,
    BackgroundMapImmediateSingleSource>;

enum class BackgroundMapPpcSingleOpcode : std::uint8_t {
    CopyWord,
    Fsubs,
    Fmadds,
    Fmuls,
    Fdivs,
};

struct BackgroundMapPpcSingleOperation {
    BackgroundMapPpcSingleOpcode opcode =
        BackgroundMapPpcSingleOpcode::CopyWord;

    std::array<BackgroundMapPpcOperand, 3> operands{};
    std::uint8_t operand_count = 0;
};

struct BackgroundMapConstantComponentRecipe {
    std::uint32_t destination_register = 0;
    BackgroundMapVectorComponent destination_component =
        BackgroundMapVectorComponent::X;
    std::array<BackgroundMapPpcSingleOperation, 4> operations{};
    std::uint8_t operation_count = 0;
};

struct BackgroundMapConstantProvenance {
    std::array<BackgroundMapFloat4CopySource, 4> copied_float4s{};
    BackgroundMapContextVectorPath context_vector{};
    std::array<BackgroundMapConstantComponentRecipe, 4>
        c76_components{};
};

struct BackgroundMapConstantUpload {
    bool present = false;
    std::uint32_t first_register = 0;
    std::uint32_t register_count = 0;
    std::uint64_t dirty_mask = 0;

    bool guard_known = false;
    bool guard_enabled = false;
    bool values_known = false;

    std::array<RawFloat4, 5> payload_words{};
    BackgroundMapConstantProvenance provenance{};
};

struct BackgroundMapSetup {
    BackgroundMapAction action = BackgroundMapAction::None;
    std::uint32_t argument = 0;

    bool writes_boolean_b129 = false;
    bool boolean_b129 = false;
    std::array<BackgroundMapResourceBinding, 2> resource_bindings{};
    std::size_t resource_binding_count = 0;
    std::array<BackgroundMapSamplerMethodWrite, 11>
        sampler_method_writes{};
    std::size_t sampler_method_write_count = 0;
    BackgroundMapConstantUpload constant_upload{};
};

enum class DirectConstantUploadId : std::uint8_t {
    None,
    VertexC0C3,
    PixelC28,
};

enum class DrawEventKind : std::uint8_t {
    PrepareResource,
    SelectVertexShader,
    RequestRenderState,
    ConditionalDirectUploadCallSite,
    SelectPixelShader,
    ConstantSetterCall,
    BackgroundMapBindOrClear,
    ProcessLogicalResourceBinding,
    SubmitDraw,
};

constexpr std::size_t kNoDrawEventRecord =
    static_cast<std::size_t>(-1);

struct DrawEvent {
    DrawEventKind kind = DrawEventKind::PrepareResource;
    std::size_t record_index = kNoDrawEventRecord;
    DirectConstantUploadId direct_upload = DirectConstantUploadId::None;
};

struct DrawInputs {
    std::uint32_t render_context = 0;

    std::uint32_t background_map_argument = 0;
    bool background_maps_enabled = false;
    bool suppress_background_map_binding = false;

    bool inherited_c20_w_known = false;
    float inherited_c20_w = 0.0f;
    bool background_map_constant_guard_known = false;
    bool background_map_constant_guard_enabled = false;
    bool background_map_constants_known = false;
    std::array<RawFloat4, 5> background_map_constant_words_c72_c76{};
    bool vertex_c0_c3_upload_guard_known = false;
    bool vertex_c0_c3_upload_guard_enabled = false;
    bool vertex_constant_words_c0_c3_known = false;
    bool pixel_c28_upload_guard_known = false;
    bool pixel_c28_upload_guard_enabled = false;
    PreparedResourceInput primary_resource{};
    PreparedResourceInput secondary_resource{};
    bool logical156_default_payload_known = false;
    std::uint32_t logical156_default_payload = 0;

    Float4 viewer_position{};
    Float4 viewer_direction{};

    std::array<RawFloat4, 4> vertex_constant_words_c0_c3{};

    bool has_environment_item = false;
    Float4 light_direction{};
    Float4 light_colour{};
};

struct DrawSetup {

    bool caller_preconditions_met = false;
    bool resource_inputs_consistent = false;
    bool draw_decision_known = false;
    bool aborted_secondary_pending = false;
    bool aborted_primary_pending = false;
    bool should_draw = false;
    std::uint32_t vertex_shader_entry = 0;
    std::uint32_t pixel_shader_entry = 0;
    std::uint32_t primary_resource_object = 0;
    std::uint32_t secondary_resource_object = 0;
    VertexC0C3DirectUpload vertex_c0_c3_upload{};
    PixelC28DirectUpload pixel_c28_upload{};
    std::vector<ConstantSetterCall> constant_calls;
    std::vector<RenderStateWrite> render_state_writes;
    std::vector<InheritedStateRequirement> inherited_state_requirements;
    std::vector<SamplerStateWrite> sampler_state_writes;
    std::vector<InheritedSamplerStateRequirement>
        inherited_sampler_state_requirements;
    InheritedPixelConstantRequirement inherited_c20_w{};
    TextureFetchControls density_fetch_controls{};
    std::vector<ResourcePrepareCall> resource_prepare_calls;
    std::vector<ResourceBindingCall> resource_binding_calls;
    BackgroundMapSetup background_map{};
    std::vector<DrawEvent> events;
};

struct LayerPassSetup {
    std::vector<RenderStateWrite> before_draw;
    DrawSetup draw;
    std::vector<RenderStateWrite> after_draw;
};

LayerConfig BuildConfig(const LayerThemeValues& theme);
void InitialiseLayer(LayerRuntimeXex& layer);

bool UpdateLayer(LayerRuntimeXex& layer,
                 const LayerConfig& next,
                 double current_time,
                 DensityResourceBinding& current_density_resource,
                 const DensityResourceBinding& resolved_density_resource,
                 std::vector<DensityResourceLifetimeEvent>*
                     lifetime_events = nullptr);

std::uint32_t SelectDensityToken(
    const std::vector<DensityCandidate>& candidates);

bool IsActive(const LayerRuntimeXex& layer);
ActiveOrder BuildActiveOrder(
    const std::array<LayerRuntimeXex, kLayerCount>& layers);

std::array<Vertex, 4> BuildVertices(const LayerConfig& config);
const std::array<std::uint16_t, 6>& Indices();
const std::array<VertexElement, 2>& VertexDeclaration();

DrawSetup BuildDrawSetup(const LayerRuntimeXex& layer,
                         const DrawInputs& inputs);
LayerPassSetup BuildLayerPassSetup(const LayerRuntimeXex& layer,
                                  const DrawInputs& inputs);

float ShaderNormalStrength(float authored_normal_strength);

std::uint32_t AlphaTestReference(const LayerConfig& config,
                                 bool render_context_two = false);

float AlphaTestThreshold(const LayerConfig& config,
                         bool render_context_two = false);

}
