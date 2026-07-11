#pragma once

#include "XenosShaderBinary.h"

#include <array>
#include <cstdint>

namespace CloudBackgroundGeneration {

enum class InputTextureRole : std::uint8_t {
    LookupF13,
    GradientF14,
};

struct InputTextureRecipe {
    InputTextureRole role = InputTextureRole::LookupF13;
    std::uint32_t create_call_site = 0;
    std::uint32_t create_function = 0;
    std::uint32_t owner_wrapper_pointer_offset = 0;
    std::uint32_t resource_pointer_offset_in_wrapper = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t depth = 0;
    std::uint32_t level_count = 0;
    std::uint32_t format_table_index = 0;
    std::uint32_t format_table_entry_address = 0;
    std::uint32_t format_word = 0;
    std::uint32_t data_format = 0;
    std::uint32_t data_format_name_address = 0;
    std::uint32_t payload_bytes = 0;
    std::uint32_t source_table_address = 0;
    std::uint32_t source_element_stride = 0;
    std::uint32_t source_element_count = 0;
    bool take_loaded_dword_low_byte = false;
    std::array<std::uint8_t, 32> source_sha256{};
    std::uint32_t gradient_scale_word = 0;
    std::uint32_t gradient_scale_address = 0;
    std::uint32_t constructor_argument_5 = 0;
    std::uint32_t constructor_argument_6 = 0;
    std::uint32_t constructor_argument_7 = 0;
    std::uint32_t internal_flags_or = 0;
    std::uint32_t upload_call_site = 0;
    std::uint32_t upload_function = 0;
    std::uint32_t upload_final_argument = 0;
    std::array<std::uint8_t, 32> payload_sha256{};
};

struct ResourceBindingRecipe {
    std::uint32_t logical_id = 0;
    std::uint32_t hardware_slot = 0;
    std::uint32_t owner_wrapper_pointer_offset = 0;
    std::uint32_t resource_pointer_offset_in_wrapper = 0;
};

struct ConstantRecipe {
    std::uint32_t logical_id = 0;
    std::uint32_t hardware_register = 0;
    std::array<std::uint32_t, 4> object_component_offsets{};
    std::uint32_t component_count = 0;
    bool z_is_object_float_times_current_time_then_frsp = false;
    std::uint32_t current_time_address = 0;
    std::uint32_t w_immediate_word = 0;
};

struct GenerationPassRecipe {
    std::uint32_t function_address = 0;
    std::uint32_t vertex_shader_entry = 0;
    std::uint32_t pixel_shader_entry = 0;
    std::array<ResourceBindingRecipe, 2> resource_bindings{};
    std::array<ConstantRecipe, 2> constants{};
    std::uint32_t destination_wrapper_field_offset = 0;
    std::uint32_t destination_resource_field_offset = 0;
    std::uint32_t install_render_target_function = 0;
    std::uint32_t clear_function = 0;
    std::uint32_t clear_packed_word_address = 0;
    std::uint32_t clear_packed_word = 0;
    std::uint32_t vertex_declaration_address = 0;
    std::array<XenosShaderBinary::VertexDeclarationElement, 2>
        vertex_elements{};
    std::array<std::array<std::uint32_t, 3>, 2>
        patched_vertex_fetch_words{};
    std::array<std::uint16_t, 6> indices{};
    std::uint32_t vertex_count = 0;
    std::uint32_t vertex_stride_bytes = 0;
    std::uint32_t primitive_type = 0;
    std::uint32_t primitive_count = 0;
    std::uint32_t quad_builder_function = 0;
    std::uint32_t draw_function = 0;
    std::uint32_t resolve_function = 0;
    std::uint32_t literal_upload_descriptor_offset = 0;
    std::uint16_t literal_upload_target = 0;
    std::uint16_t literal_upload_count = 0;
    std::uint32_t literal_upload_source_offset = 0;
    std::array<std::array<std::uint32_t, 4>, 4>
        embedded_pixel_constants_c252_c255{};
};

const std::array<InputTextureRecipe, 2>& ExactInputTextureRecipes() noexcept;
const std::array<std::uint8_t, 256>& ExactLookupPayload() noexcept;
const std::array<std::uint32_t, 49>& ExactGradientSourceWords() noexcept;
const std::array<std::uint8_t, 64>& ExactGradientPayload() noexcept;
const GenerationPassRecipe& ExactGenerationPassRecipe() noexcept;

}  // namespace CloudBackgroundGeneration
