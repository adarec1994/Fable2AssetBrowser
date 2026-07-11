#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <variant>
#include <vector>

namespace CloudRuntime {

// Exact structural data (offsets, raw words, branches, call/event order, and
// resource/state provenance) is kept separate from the convenience host-float
// helpers below.  The latter are not a Xenon/PPC/VMX numeric emulator: NaN
// payloads, denorm/FTZ behavior, invalid conversions, signed-zero selection,
// and exception flags remain a hardware boundary.  Authoritative replay must
// use the raw recipes/words with a proven PPC/Xenos numeric backend.

constexpr std::size_t kLayerCount = 4;

// Scalar values copied from one interpolated Clouds,LayerN theme record by
// default.xex sub_822675D0.  These are deliberately not normalised or clamped.
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

// Exact 16-word configuration copied to the first 0x40 bytes of a cloud
// layer.  The field order is recovered from sub_822675D0/sub_8227AE18.
struct LayerConfig {
    float transparency;             // 0x00
    float height;                   // 0x04
    float normal_strength;          // 0x08
    float translucency_strength;    // 0x0c (stored, unused by cloud shaders)
    float alpha_reference;          // 0x10 = 0.15000001f
    float ambient_light;            // 0x14
    float brightness;               // 0x18
    float position_x;               // 0x1c (stored, unused by cloud draw)
    float position_y;               // 0x20 (stored, unused by cloud draw)
    float size_x;                   // 0x24, rectangle half-extent
    float size_y;                   // 0x28, rectangle half-extent
    float texture_scale_x;          // 0x2c
    float texture_scale_y;          // 0x30
    float velocity_x;               // 0x34, authored value * 0.001f
    float velocity_y;               // 0x38, authored value * 0.001f
    std::uint32_t density_token;     // 0x3c
};

static_assert(sizeof(LayerConfig) == 0x40);
static_assert(offsetof(LayerConfig, alpha_reference) == 0x10);
static_assert(offsetof(LayerConfig, texture_scale_x) == 0x2c);
static_assert(offsetof(LayerConfig, velocity_x) == 0x34);
static_assert(offsetof(LayerConfig, density_token) == 0x3c);

// Layout mirror of the 88-byte PowerPC runtime object.  Resource references
// are represented by their 32-bit object token here; native D3D pointers are
// kept elsewhere.  This lets the host reproduce the XEX's resource->field_1C
// identity comparison without embedding a native pointer in this layout.
struct LayerRuntimeXex {
    LayerConfig config;                  // 0x00
    std::uint32_t density_resource;      // 0x40
    std::uint32_t secondary_resource;    // 0x44
    double last_update_time;             // 0x48
    float uv_x;                          // 0x50
    float uv_y;                          // 0x54
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

// Host-side mirror of the identity fields involved in 0x8227AE18.  The XEX
// stores object_token in LayerRuntimeXex::density_resource and reads the
// authored identity from object+0x1C; keeping the two values distinct avoids
// the incorrect assumption that a resource pointer equals its authored token.
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

// Intrusive-reference operations performed by 0x8227AE18.  The host token
// model records them in order; a native backend must apply the refcount and
// virtual destruction semantics to its actual resource objects.
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

// One logical setter invocation from 0x8223A1E8, retained in call order.
// has_shader_binding is false for a setter that the selected release shader
// proves is a no-op (g_LightDirection / logical 126).
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

// A sampler/fetch word field which CloudLayer_Draw inherits rather than
// writing.  initial_device_value is the exact fresh-device value established
// by 0x82B9FB28; it is not promoted to a call-site value because intervening
// draws may change it.
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

// Fields carried by the Xenos texture-fetch instruction itself.  These are
// not D3D sampler writes and must remain distinct from inherited slot state.
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

// Host snapshot of the exact object fields consumed after
// RenderResource_Prepare(obj,0,0,0): the helper returns obj+0x20, its +4
// DWORD is passed to the binder, and the streaming check reads obj+0x54.
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
    // Raw device-method offset used by the XEX record dispatcher.  It is
    // intentionally not assigned a guessed D3D sampler-state name.
    std::uint32_t method_offset = 0;
    std::uint32_t value = 0;
};

// Direct shader constants are transported as words.  Converting them to host
// float would canonicalise some NaNs and lose payload/signalling bits before
// the Xenos upload is even attempted.
using RawFloat4 = std::array<std::uint32_t, 4>;

enum class DirectConstantStage : std::uint8_t {
    Vertex,
    Pixel,
};

// Metadata for a conditional direct-register upload site.  call_site_reached
// is independent from the guard: guard_known && guard_enabled means the XEX
// performed the upload, while a reached site with a false/unknown guard is
// still retained in the ordered event stream.
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

// c72-c75 are direct 16-byte copies from the selected category-27 object.
struct BackgroundMapFloat4CopySource {
    std::uint32_t destination_register = 0;
    std::uint32_t object_byte_offset = 0;
};

// The vector used by the c76 recipe is reached as
// *(*(render_context + pointer_offsets[0]) + pointer_offsets[1]) +
// vector_byte_offset.
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

// A variant keeps memory sources, literal bit patterns, and operation
// results distinct.  No source is represented by a host floating value.
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
    // Operands follow PPC arithmetic order.  Fmadds is
    // (operands[0] * operands[1]) + operands[2].
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
    // The call is gated by qword_83491590 bit19.  Unknown means the setup
    // records the conditional upload without pretending the guard is false.
    bool guard_known = false;
    bool guard_enabled = false;
    bool values_known = false;
    // Raw c72-c76 register words in upload order.  Their values are supplied
    // by an exact PPC evaluator or capture, never calculated by this host
    // metadata layer.
    std::array<RawFloat4, 5> payload_words{};
    BackgroundMapConstantProvenance provenance{};
};

// Operation selected at 0x8223A6E0..0x8223A714.  Shader 137 selection and
// background-map binding deliberately remain separate decisions.
struct BackgroundMapSetup {
    BackgroundMapAction action = BackgroundMapAction::None;
    std::uint32_t argument = 0;
    // Pixel Boolean logical index 1 is hardware b129.  Action::None means
    // CloudLayer_Draw made no write and the Boolean remains inherited.
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

// record_index names an element in resource_prepare_calls,
// render_state_writes, constant_calls, or resource_binding_calls according to
// kind.  The two shader-selection, background, and submit events use no
// record; direct-upload events instead identify one of the typed upload
// records above.
struct DrawEvent {
    DrawEventKind kind = DrawEventKind::PrepareResource;
    std::size_t record_index = kNoDrawEventRecord;
    DirectConstantUploadId direct_upload = DirectConstantUploadId::None;
};

struct DrawInputs {
    std::uint32_t render_context = 0;
    // Actual r5/a3 value passed through 0x82267818 -> 0x821DB170.  It is a
    // handle, not a boolean, and is forwarded to BgMap_BindForRender.
    std::uint32_t background_map_argument = 0;
    bool background_maps_enabled = false;       // g_BackgroundMapsEnabled
    bool suppress_background_map_binding = false; // context + 0x70A != 0
    // Renderer-global pixel c20.w inherited by PS114/115.  The ordinary
    // context-2 path supplies the dynamic +0x41AC field; callers may leave it
    // unknown until that renderer state is modeled.
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

    Float4 viewer_position{};   // context + 0x690
    Float4 viewer_direction{};  // context + 0x6A0
    // VS113 CTAB names these four registers g_WorldViewProjection.  The XEX
    // copies their exact words from context+0x6B0.
    std::array<RawFloat4, 4> vertex_constant_words_c0_c3{};

    bool has_environment_item = false;
    Float4 light_direction{};   // environment item + 0x130
    Float4 light_colour{};      // environment item + 0x0D0
};

// Proven draw-side inputs and state writes from default.xex 0x8223A1E8.
// Inherited state is recorded separately from both actual setter calls and
// the controls encoded in the shader's texture-fetch instruction.
struct DrawSetup {
    // CloudLayers_UpdateSortDraw filters out null primary resources before
    // reaching CloudLayer_Draw.  A false value is a host-side rejection of
    // an invalid call chain, not a branch present in the XEX inner draw.
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

// Exact caller protocol from 0x821DB170.  Context 1 forces the background
// argument to zero and brackets the inner draw with COLORWRITE changes;
// other contexts request state 48 = 0 before the inner draw restores it.
struct LayerPassSetup {
    std::vector<RenderStateWrite> before_draw;
    DrawSetup draw;
    std::vector<RenderStateWrite> after_draw;
};

LayerConfig BuildConfig(const LayerThemeValues& theme);
void InitialiseLayer(LayerRuntimeXex& layer);

// Mirrors sub_822675D0: motion uses the previous config's velocities, wraps
// with x-floor(x), and only then installs the newly interpolated config.
// Returns false without mutation if the host binding metadata does not name
// layer.density_resource, or if a non-null resolver result does not carry the
// requested object+0x1C identity.  Those are host-model consistency checks;
// the XEX obtains both values from the actual resource objects.
bool UpdateLayer(LayerRuntimeXex& layer,
                 const LayerConfig& next,
                 double current_time,
                 DensityResourceBinding& current_density_resource,
                 const DensityResourceBinding& resolved_density_resource,
                 std::vector<DensityResourceLifetimeEvent>*
                     lifetime_events = nullptr);

// Exact a4=1 token result of 0x821D08C0, including the PPC unordered-float
// comparisons and u32 tie-break used by 0x82AB2158/0x82AB22F0.
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

// sub_8223A1E8 transforms the authored strength before binding c70.
float ShaderNormalStrength(float authored_normal_strength);

// Fixed-function alpha-test reference emitted by sub_8223A1E8.  Render
// context 2 uses integer reference 5; the depth path truncates
// config.alpha_reference * 255.
std::uint32_t AlphaTestReference(const LayerConfig& config,
                                 bool render_context_two = false);
// D3D preview compatibility only.  The XEX never converts the reference back
// to float; exact execution must submit AlphaTestReference as 8-bit state 100
// with compare-function state 104 set to GREATER (4).
float AlphaTestThreshold(const LayerConfig& config,
                         bool render_context_two = false);

}  // namespace CloudRuntime
