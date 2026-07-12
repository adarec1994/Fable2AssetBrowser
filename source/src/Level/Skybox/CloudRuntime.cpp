#include "CloudRuntime.h"
#include "CloudShaderBank.h"

#include <algorithm>
#include <cmath>

namespace CloudRuntime {

namespace {

constexpr float kAlphaReference = 0.15000001f;
constexpr float kVelocityScale = 0.001f;
constexpr double kFloorMagnitudeLimit = 1.0e18;

float RoundSingle(float value)
{

    volatile float rounded = value;
    return rounded;
}

double CloudFloorPpc(double value)
{

    const double magnitude = std::fabs(value);
    if (magnitude == 0.0 || !(magnitude <= kFloorMagnitudeLimit)) {
        return value;
    }
    const double truncated = std::trunc(value);
    const double remainder = value - truncated;
    return remainder >= 0.0 ? truncated : truncated - 1.0;
}

float WrapUnit(float value)
{
    const float floor_value =
        RoundSingle(static_cast<float>(CloudFloorPpc(value)));
    return RoundSingle(value - floor_value);
}

bool DensityLess(const DensityCandidate& lhs,
                 const DensityCandidate& rhs)
{

    if (lhs.weight < rhs.weight) return true;
    if (rhs.weight < lhs.weight) return false;
    return lhs.token < rhs.token;
}

void DensityAdjustHeap(std::vector<DensityCandidate>& values,
                       std::size_t hole,
                       std::size_t length,
                       DensityCandidate moving)
{

    const std::size_t top = hole;
    std::size_t child = 2 * (hole + 1);
    while (child < length) {
        if (DensityLess(values[child - 1], values[child])) {
            --child;
        }
        values[hole] = values[child];
        hole = child;
        child = 2 * (child + 1);
    }
    if (child == length) {
        values[hole] = values[length - 1];
        hole = length - 1;
    }

    while (top < hole) {
        const std::size_t parent = (hole - 1) / 2;
        if (!DensityLess(moving, values[parent])) break;
        values[hole] = values[parent];
        hole = parent;
    }
    values[hole] = moving;
}

void DensityPartialSortTopTwo(std::vector<DensityCandidate>& values)
{
    const std::size_t middle = 2;
    std::size_t parent = middle / 2;
    while (parent > 0) {
        --parent;
        const DensityCandidate moving = values[parent];
        DensityAdjustHeap(values, parent, middle, moving);
    }

    for (std::size_t current = middle; current < values.size(); ++current) {
        if (DensityLess(values[0], values[current])) {
            const DensityCandidate moving = values[current];
            values[current] = values[0];
            DensityAdjustHeap(values, 0, middle, moving);
        }
    }

    std::size_t heap_end = middle;
    while (heap_end > 1) {
        --heap_end;
        const DensityCandidate moving = values[heap_end];
        values[heap_end] = values[0];
        DensityAdjustHeap(values, 0, heap_end, moving);
    }
}

const BackgroundMapConstantProvenance& ExactBackgroundMapConstantProvenance()
{

    static const BackgroundMapConstantProvenance provenance = [] {
        BackgroundMapConstantProvenance result{};
        result.copied_float4s = {{
            {72, 0x0c0},
            {73, 0x160},
            {74, 0x170},
            {75, 0x140},
        }};
        result.context_vector.pointer_offsets = {4, 20};
        result.context_vector.vector_byte_offset = 0x0be0;

        auto& c76_x = result.c76_components[0];
        c76_x.destination_register = 76;
        c76_x.destination_component = BackgroundMapVectorComponent::X;
        c76_x.operations[0].opcode =
            BackgroundMapPpcSingleOpcode::CopyWord;
        c76_x.operations[0].operands[0] =
            BackgroundMapContextComponentSource{
                BackgroundMapVectorComponent::Z};
        c76_x.operations[0].operand_count = 1;
        c76_x.operation_count = 1;

        auto& c76_y = result.c76_components[1];
        c76_y.destination_register = 76;
        c76_y.destination_component = BackgroundMapVectorComponent::Y;
        c76_y.operations[0].opcode =
            BackgroundMapPpcSingleOpcode::CopyWord;
        c76_y.operations[0].operands[0] =
            BackgroundMapObjectWordSource{0x0f8};
        c76_y.operations[0].operand_count = 1;
        c76_y.operation_count = 1;

        auto& c76_z = result.c76_components[2];
        c76_z.destination_register = 76;
        c76_z.destination_component = BackgroundMapVectorComponent::Z;
        c76_z.operations[0].opcode =
            BackgroundMapPpcSingleOpcode::Fsubs;
        c76_z.operations[0].operands[0] =
            BackgroundMapContextComponentSource{
                BackgroundMapVectorComponent::X};
        c76_z.operations[0].operands[1] =
            BackgroundMapImmediateSingleSource{0x3f800000};
        c76_z.operations[0].operand_count = 2;

        c76_z.operations[1].opcode =
            BackgroundMapPpcSingleOpcode::Fmadds;
        c76_z.operations[1].operands[0] =
            BackgroundMapPreviousOperationSource{0};
        c76_z.operations[1].operands[1] =
            BackgroundMapObjectWordSource{0x158};
        c76_z.operations[1].operands[2] =
            BackgroundMapImmediateSingleSource{0x3f800000};
        c76_z.operations[1].operand_count = 3;

        c76_z.operations[2].opcode =
            BackgroundMapPpcSingleOpcode::Fmuls;
        c76_z.operations[2].operands[0] =
            BackgroundMapPreviousOperationSource{1};
        c76_z.operations[2].operands[1] =
            BackgroundMapObjectWordSource{0x0b8};
        c76_z.operations[2].operand_count = 2;

        c76_z.operations[3].opcode =
            BackgroundMapPpcSingleOpcode::Fmuls;
        c76_z.operations[3].operands[0] =
            BackgroundMapPreviousOperationSource{2};
        c76_z.operations[3].operands[1] =
            BackgroundMapImmediateSingleSource{0xbfb8aa3b};
        c76_z.operations[3].operand_count = 2;
        c76_z.operation_count = 4;

        auto& c76_w = result.c76_components[3];
        c76_w.destination_register = 76;
        c76_w.destination_component = BackgroundMapVectorComponent::W;
        c76_w.operations[0].opcode =
            BackgroundMapPpcSingleOpcode::Fdivs;
        c76_w.operations[0].operands[0] =
            BackgroundMapImmediateSingleSource{0x3f800000};
        c76_w.operations[0].operands[1] =
            BackgroundMapObjectWordSource{0x13c};
        c76_w.operations[0].operand_count = 2;
        c76_w.operation_count = 1;
        return result;
    }();
    return provenance;
}

}

LayerConfig BuildConfig(const LayerThemeValues& theme)
{

    LayerConfig result{};
    result.transparency = theme.transparency;
    result.height = theme.height;
    result.normal_strength = theme.normal_strength;
    result.translucency_strength = theme.translucency_strength;
    result.alpha_reference = kAlphaReference;
    result.ambient_light = theme.ambient_light;
    result.brightness = theme.brightness;
    result.position_x = theme.position_x;
    result.position_y = theme.position_y;
    result.size_x = theme.size_x;
    result.size_y = theme.size_y;
    result.texture_scale_x = theme.texture_scale_x;
    result.texture_scale_y = theme.texture_scale_y;
    result.velocity_x = theme.velocity_x * kVelocityScale;
    result.velocity_y = theme.velocity_y * kVelocityScale;
    result.density_token = theme.density_token;
    return result;
}

void InitialiseLayer(LayerRuntimeXex& layer)
{

    layer = {};
    layer.config.texture_scale_x = 0.001f;
    layer.config.texture_scale_y = 0.001f;
}

bool UpdateLayer(LayerRuntimeXex& layer,
                 const LayerConfig& next,
                 double current_time,
                 DensityResourceBinding& current_density_resource,
                 const DensityResourceBinding& resolved_density_resource,
                 std::vector<DensityResourceLifetimeEvent>* lifetime_events)
{

    if (lifetime_events != nullptr) lifetime_events->clear();
    if (current_density_resource.object_token != layer.density_resource) {
        return false;
    }
    const bool refresh_resource =
        layer.density_resource == 0 ||
        current_density_resource.identity_token != next.density_token;
    if (refresh_resource && next.density_token != 0 &&
        resolved_density_resource.object_token != 0 &&
        resolved_density_resource.identity_token != next.density_token) {
        return false;
    }

    const float delta_time = RoundSingle(
        static_cast<float>(current_time - layer.last_update_time));
    layer.last_update_time = current_time;

    const float step_x = RoundSingle(
        layer.config.velocity_x * delta_time);
    const float step_y = RoundSingle(
        layer.config.velocity_y * delta_time);
    layer.uv_x = WrapUnit(RoundSingle(step_x + layer.uv_x));
    layer.uv_y = WrapUnit(RoundSingle(layer.uv_y + step_y));

    layer.config = next;
    if (refresh_resource) {
        if (next.density_token == 0) {

            if (lifetime_events != nullptr && layer.density_resource != 0) {
                lifetime_events->push_back({
                    DensityResourceLifetimeEventKind::
                        ReleaseLayerReferenceMayDestroy,
                    layer.density_resource,
                    current_density_resource.identity_token,
                });
            }
            if (lifetime_events != nullptr) {
                lifetime_events->push_back({
                    DensityResourceLifetimeEventKind::AssignLayerPointer,
                    0,
                    0,
                });
            }
            layer.density_resource = 0;
            current_density_resource = {};
        } else {
            if (lifetime_events != nullptr) {
                lifetime_events->push_back({
                    DensityResourceLifetimeEventKind::ResolveIdentityToken,
                    resolved_density_resource.object_token,
                    next.density_token,
                });
            }
            if (layer.density_resource !=
                resolved_density_resource.object_token) {
                if (lifetime_events != nullptr &&
                    layer.density_resource != 0) {
                    lifetime_events->push_back({
                        DensityResourceLifetimeEventKind::
                            ReleaseLayerReferenceMayDestroy,
                        layer.density_resource,
                        current_density_resource.identity_token,
                    });
                    lifetime_events->push_back({
                        DensityResourceLifetimeEventKind::AssignLayerPointer,
                        0,
                        0,
                    });
                }
                if (lifetime_events != nullptr) {
                    lifetime_events->push_back({
                        DensityResourceLifetimeEventKind::AssignLayerPointer,
                        resolved_density_resource.object_token,
                        resolved_density_resource.identity_token,
                    });
                    if (resolved_density_resource.object_token != 0) {
                        lifetime_events->push_back({
                            DensityResourceLifetimeEventKind::
                                AddLayerReference,
                            resolved_density_resource.object_token,
                            resolved_density_resource.identity_token,
                        });
                    }
                }
            }
            layer.density_resource = resolved_density_resource.object_token;
            current_density_resource = resolved_density_resource;
            if (lifetime_events != nullptr &&
                resolved_density_resource.object_token != 0) {
                lifetime_events->push_back({
                    DensityResourceLifetimeEventKind::
                        ReleaseResolverTemporaryMayDestroy,
                    resolved_density_resource.object_token,
                    resolved_density_resource.identity_token,
                });
            }
        }
    }
    return true;
}

std::uint32_t SelectDensityToken(
    const std::vector<DensityCandidate>& candidates)
{
    if (candidates.empty()) return 0;
    if (candidates.size() == 1) return candidates[0].token;
    std::vector<DensityCandidate> sorted = candidates;
    DensityPartialSortTopTwo(sorted);
    return sorted[0].token;
}

bool IsActive(const LayerRuntimeXex& layer)
{
    return layer.density_resource != 0 && layer.config.transparency > 0.0f;
}

ActiveOrder BuildActiveOrder(
    const std::array<LayerRuntimeXex, kLayerCount>& layers)
{

    ActiveOrder result;
    for (std::uint8_t i = 0; i < kLayerCount; ++i) {
        if (IsActive(layers[i])) {
            result.indices[result.count++] = i;
        }
    }

    auto before = [&](std::uint8_t lhs, std::uint8_t rhs) {
        return layers[lhs].config.height > layers[rhs].config.height;
    };
    for (std::size_t current = 1; current < result.count; ++current) {
        const std::uint8_t moving = result.indices[current];
        std::size_t destination = current;
        if (before(moving, result.indices[0])) {
            destination = 0;
        } else if (before(moving, result.indices[current - 1])) {
            destination = current - 1;
            while (destination != 0 &&
                   before(moving, result.indices[destination - 1])) {
                --destination;
            }
        }
        for (std::size_t at = current; at > destination; --at) {
            result.indices[at] = result.indices[at - 1];
        }
        result.indices[destination] = moving;
    }
    return result;
}

std::array<Vertex, 4> BuildVertices(const LayerConfig& config)
{

    return {{
        {-config.size_x, -config.size_y, config.height, 0.0f, 0.0f},
        { config.size_x, -config.size_y, config.height, 1.0f, 0.0f},
        {-config.size_x,  config.size_y, config.height, 0.0f, 1.0f},
        { config.size_x,  config.size_y, config.height, 1.0f, 1.0f},
    }};
}

const std::array<std::uint16_t, 6>& Indices()
{
    static constexpr std::array<std::uint16_t, 6> kIndices = {
        0, 1, 2, 1, 3, 2
    };
    return kIndices;
}

const std::array<VertexElement, 2>& VertexDeclaration()
{

    static constexpr std::array<VertexElement, 2> kDeclaration = {{
        {0, 0,  2, 0, 0},
        {0, 12, 1, 5, 0},
    }};
    return kDeclaration;
}

DrawSetup BuildDrawSetup(const LayerRuntimeXex& layer,
                         const DrawInputs& inputs)
{
    DrawSetup setup{};
    setup.primary_resource_object = layer.density_resource;
    setup.secondary_resource_object = layer.secondary_resource;

    setup.caller_preconditions_met = layer.density_resource != 0;
    if (!setup.caller_preconditions_met) {
        return setup;
    }

    setup.resource_inputs_consistent =
        inputs.primary_resource.present &&
        inputs.primary_resource.object == layer.density_resource &&
        ((layer.secondary_resource == 0 &&
          !inputs.secondary_resource.present) ||
         (layer.secondary_resource != 0 &&
          inputs.secondary_resource.present &&
          inputs.secondary_resource.object == layer.secondary_resource));
    if (!setup.resource_inputs_consistent) {
        return setup;
    }

    setup.resource_prepare_calls.push_back({
        layer.density_resource, 0x20, 0x24,
        inputs.primary_resource.prepare_result_plus4_payload_known,
        inputs.primary_resource.prepare_result_plus4_payload,
        inputs.primary_resource.object_plus54_state_known,
        inputs.primary_resource.object_plus54_state,
    });
    setup.events.push_back({DrawEventKind::PrepareResource, 0});
    if (layer.secondary_resource != 0) {
        setup.resource_prepare_calls.push_back({
            layer.secondary_resource, 0x20, 0x24,
            inputs.secondary_resource.prepare_result_plus4_payload_known,
            inputs.secondary_resource.prepare_result_plus4_payload,
            inputs.secondary_resource.object_plus54_state_known,
            inputs.secondary_resource.object_plus54_state,
        });
        setup.events.push_back({DrawEventKind::PrepareResource, 1});
    }

    if (layer.secondary_resource != 0) {
        if (!inputs.secondary_resource.object_plus54_state_known) {
            return setup;
        }
        if (inputs.secondary_resource.object_plus54_state == 0x7fffffffu) {
            setup.draw_decision_known = true;
            setup.aborted_secondary_pending = true;
            return setup;
        }
    }
    if (!inputs.primary_resource.object_plus54_state_known) {
        return setup;
    }
    setup.draw_decision_known = true;
    if (inputs.primary_resource.object_plus54_state == 0x7fffffffu) {
        setup.aborted_primary_pending = true;
        return setup;
    }

    setup.should_draw = true;
    const bool mode_two = inputs.render_context == 2;

    setup.vertex_shader_entry = CloudShaderBank::kVertexShaderEntry;
    setup.events.push_back({DrawEventKind::SelectVertexShader});
    setup.render_state_writes.push_back({56, 0});
    setup.events.push_back({DrawEventKind::RequestRenderState, 0});

    setup.vertex_c0_c3_upload.call_site_reached = true;
    setup.vertex_c0_c3_upload.stage = DirectConstantStage::Vertex;
    setup.vertex_c0_c3_upload.first_register = 0;
    setup.vertex_c0_c3_upload.register_count = 4;

    setup.vertex_c0_c3_upload.dirty_mask = 0x8000000000000000ull;
    setup.vertex_c0_c3_upload.guard_known =
        inputs.vertex_c0_c3_upload_guard_known;
    setup.vertex_c0_c3_upload.guard_enabled =
        inputs.vertex_c0_c3_upload_guard_enabled;
    setup.vertex_c0_c3_upload.payload_known =
        inputs.vertex_constant_words_c0_c3_known;
    setup.vertex_c0_c3_upload.payload_words =
        inputs.vertex_constant_words_c0_c3;
    setup.events.push_back({
        DrawEventKind::ConditionalDirectUploadCallSite,
        kNoDrawEventRecord,
        DirectConstantUploadId::VertexC0C3});

    setup.pixel_shader_entry = CloudShaderBank::SelectPixelShaderEntry(
        inputs.render_context, inputs.background_map_argument);
    setup.events.push_back({DrawEventKind::SelectPixelShader});

    setup.inherited_c20_w = {
        20, 3, mode_two,
        mode_two ? inputs.inherited_c20_w_known : true,
        mode_two ? inputs.inherited_c20_w : 1.0f,
    };

    auto scalar = [&](std::uint32_t id, float value, bool active = true) {
        setup.constant_calls.push_back({
            id, ConstantKind::Scalar, {value, 0.0f, 0.0f, 0.0f}, active});
        setup.events.push_back({
            DrawEventKind::ConstantSetterCall,
            setup.constant_calls.size() - 1});
    };
    auto vector = [&](std::uint32_t id, Float4 value,
                      bool active = true) {
        setup.constant_calls.push_back({
            id, ConstantKind::Vector, value, active});
        setup.events.push_back({
            DrawEventKind::ConstantSetterCall,
            setup.constant_calls.size() - 1});
    };
    auto float2 = [&](std::uint32_t id, float x, float y,
                      bool active = true) {
        setup.constant_calls.push_back({
            id, ConstantKind::Float2, {x, y, 0.0f, 0.0f}, active});
        setup.events.push_back({
            DrawEventKind::ConstantSetterCall,
            setup.constant_calls.size() - 1});
    };

    vector(38, {layer.config.texture_scale_x,
                layer.config.texture_scale_y, 0.0f, 0.0f});

    float2(0, layer.uv_x, layer.uv_y);
    vector(272, inputs.viewer_position);
    scalar(264, layer.config.transparency);

    if (mode_two) {
        vector(271, inputs.viewer_direction);
        scalar(157, ShaderNormalStrength(layer.config.normal_strength));
        scalar(9, layer.config.ambient_light);
        scalar(23, layer.config.brightness);

        const Float4 direction = inputs.has_environment_item
            ? inputs.light_direction : Float4{};
        const Float4 colour = inputs.has_environment_item
            ? inputs.light_colour : Float4{};

        vector(126, direction, false);
        vector(122, colour);

        Float4 position{};
        if (inputs.has_environment_item) {
            position.x = RoundSingle(inputs.viewer_position.x -
                RoundSingle(direction.x * 2000.0f));
            position.y = RoundSingle(inputs.viewer_position.y -
                RoundSingle(direction.y * 2000.0f));
            position.z = RoundSingle(inputs.viewer_position.z -
                RoundSingle(direction.z * 2000.0f));
            position.w = RoundSingle(inputs.viewer_position.w -
                RoundSingle(direction.w * 2000.0f));
        }
        vector(131, position);

        setup.pixel_c28_upload.call_site_reached = true;
        setup.pixel_c28_upload.stage = DirectConstantStage::Pixel;
        setup.pixel_c28_upload.first_register = 28;
        setup.pixel_c28_upload.register_count = 1;

        setup.pixel_c28_upload.dirty_mask = 0x0100000000000000ull;
        setup.pixel_c28_upload.guard_known =
            inputs.pixel_c28_upload_guard_known;
        setup.pixel_c28_upload.guard_enabled =
            inputs.pixel_c28_upload_guard_enabled;
        setup.pixel_c28_upload.payload_known = true;
        setup.pixel_c28_upload.payload_words[0] = {
            0x00000000u, 0x3f800000u, 0x00000000u, 0x3f800000u,
        };
        setup.events.push_back({
            DrawEventKind::ConditionalDirectUploadCallSite,
            kNoDrawEventRecord,
            DirectConstantUploadId::PixelC28});

        if (!inputs.suppress_background_map_binding) {
            if (inputs.background_map_argument != 0 &&
                inputs.background_maps_enabled) {
                setup.background_map.action =
                    BackgroundMapAction::BindArgument;
                setup.background_map.argument =
                    inputs.background_map_argument;
                setup.background_map.writes_boolean_b129 = true;
                setup.background_map.boolean_b129 = true;

                setup.background_map.resource_bindings = {{
                    {12, inputs.background_map_argument, 0x114, 0x80000},
                    {11, inputs.background_map_argument, 0x11c, 0x100000},
                }};
                setup.background_map.resource_binding_count = 2;

                setup.background_map.sampler_method_writes = {{
                    {12, 12, 0},
                    {12, 0, 6},
                    {12, 4, 6},
                    {12, 16, 1},
                    {12, 20, 1},
                    {12, 24, 2},
                    {11, 0, 0},
                    {11, 4, 0},
                    {11, 16, 1},
                    {11, 20, 1},
                    {11, 24, 2},
                }};
                setup.background_map.sampler_method_write_count = 11;

                setup.background_map.constant_upload.present = true;
                setup.background_map.constant_upload.first_register = 72;
                setup.background_map.constant_upload.register_count = 5;
                setup.background_map.constant_upload.dirty_mask =
                    std::uint64_t{3} << 44;
                setup.background_map.constant_upload.guard_known =
                    inputs.background_map_constant_guard_known;
                setup.background_map.constant_upload.guard_enabled =
                    inputs.background_map_constant_guard_enabled;
                setup.background_map.constant_upload.values_known =
                    inputs.background_map_constants_known;
                setup.background_map.constant_upload.payload_words =
                    inputs.background_map_constant_words_c72_c76;
                setup.background_map.constant_upload.provenance =
                    ExactBackgroundMapConstantProvenance();
            } else {
                setup.background_map.action =
                    BackgroundMapAction::BindDefault;

                setup.background_map.writes_boolean_b129 = true;
                setup.background_map.boolean_b129 = false;
            }
            setup.events.push_back({
                DrawEventKind::BackgroundMapBindOrClear});
        }
    }

    setup.resource_binding_calls.push_back({
        37,
        13,
        true,
        ResourceBindingPayloadSource::PreparedResultPlus4,
        layer.density_resource,
        inputs.primary_resource.prepare_result_plus4_payload_known,
        inputs.primary_resource.prepare_result_plus4_payload,
        0x40000,
        true,
        0,
    });
    setup.events.push_back({
        DrawEventKind::ProcessLogicalResourceBinding, 0});
    const bool has_secondary = layer.secondary_resource != 0;
    setup.resource_binding_calls.push_back({
        156,
        -1,
        false,
        has_secondary
            ? ResourceBindingPayloadSource::PreparedResultPlus4
            : ResourceBindingPayloadSource::GlobalDefault8331AF70,
        layer.secondary_resource,
        has_secondary
            ? inputs.secondary_resource.prepare_result_plus4_payload_known
            : inputs.logical156_default_payload_known,
        has_secondary
            ? inputs.secondary_resource.prepare_result_plus4_payload
            : inputs.logical156_default_payload,
        0,
        false,
        0x8331af70,
    });
    setup.events.push_back({
        DrawEventKind::ProcessLogicalResourceBinding, 1});

    const std::array<RenderStateWrite, 7> final_state_writes = {{
        {100, AlphaTestReference(layer.config, mode_two)},
        {96, 1},
        {104, 4},
        {60, 1},
        {72, 6},
        {76, 7},
        {48, 1},
    }};
    for (const RenderStateWrite& state : final_state_writes) {
        setup.render_state_writes.push_back(state);
        setup.events.push_back({
            DrawEventKind::RequestRenderState,
            setup.render_state_writes.size() - 1});
    }
    setup.events.push_back({DrawEventKind::SubmitDraw});

    const bool known_state_44 =
        inputs.render_context == 1 || inputs.render_context == 2;
    const bool known_colour_write = inputs.render_context == 1;
    setup.inherited_state_requirements = {
        {40, false, 0},
        {44, known_state_44, known_state_44 ? 6u : 0u},
        {80, false, 0},

        {84, false, 0},
        {88, false, 0},
        {92, false, 0},
        {212, known_colour_write, 0},
    };

    setup.inherited_sampler_state_requirements = {
        {13, 0, true, 0},
        {13, 1, true, 0},
        {13, 2, true, 0},
    };

    setup.density_fetch_controls = {
        true, 13, true,
        1, 1, 1, 7, 0, 3, 3, true, 0,
    };
    return setup;
}

LayerPassSetup BuildLayerPassSetup(const LayerRuntimeXex& layer,
                                   const DrawInputs& inputs)
{

    LayerPassSetup pass{};
    DrawInputs effective = inputs;
    if (inputs.render_context == 1) {
        pass.before_draw = {
            {48, 1},
            {212, 0},
        };
        effective.background_map_argument = 0;
        pass.after_draw = {
            {212, 15},
        };
    } else {
        pass.before_draw = {
            {48, 0},
        };
    }
    pass.draw = BuildDrawSetup(layer, effective);
    return pass;
}

float ShaderNormalStrength(float authored_normal_strength)
{

    float transformed = RoundSingle(1.0f - authored_normal_strength);
    if (!(transformed >= 0.1f)) transformed = 0.1f;
    if (transformed > 2.0f) transformed = 2.0f;
    return transformed;
}

std::uint32_t AlphaTestReference(const LayerConfig& config,
                                 bool render_context_two)
{

    if (render_context_two) {
        return 5;
    }
    return static_cast<std::uint32_t>(
        static_cast<std::int64_t>(config.alpha_reference * 255.0f));
}

float AlphaTestThreshold(const LayerConfig& config,
                         bool render_context_two)
{
    return static_cast<float>(
        AlphaTestReference(config, render_context_two)) * (1.0f / 255.0f);
}

}
