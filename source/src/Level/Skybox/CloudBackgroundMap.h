#pragma once

#include <array>
#include <cstdint>

namespace CloudBackgroundMap {

// Static creation recipe recovered from default.xex.  Names are assigned
// only where the XEX identifies them itself.  The final four call arguments
// intentionally stay numeric: their host-API meanings are not encoded by the
// cloud call site.
struct GeneratedTextureRecipe {
    std::uint32_t dimensions_initialise_site = 0;
    std::uint32_t create_call_site = 0;
    std::uint32_t create_function = 0;
    std::uint32_t width_field_offset = 0;
    std::uint32_t height_field_offset = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t depth = 0;
    std::uint32_t level_count = 0;
    std::uint32_t format_table_base = 0;
    std::uint32_t format_table_stride = 0;
    std::uint32_t format_table_index = 0;
    std::uint32_t format_table_entry_address = 0;
    std::array<std::uint32_t, 28> format_table_entry_words{};
    std::uint32_t data_format_mask = 0;
    std::uint32_t data_format_code = 0;
    std::uint32_t data_format_name_address = 0;
    std::array<char, 6> data_format_name{};
    std::uint32_t block_width = 0;
    std::uint32_t block_height = 0;
    std::uint32_t bits_per_element = 0;
    std::uint32_t raw_layout_selector = 0;
    std::uint32_t aligned_width = 0;
    std::uint32_t aligned_height = 0;
    std::uint32_t row_bytes = 0;
    std::uint32_t base_allocation_bytes = 0;
    std::uint32_t mip_allocation_bytes = 0;
    std::array<std::uint32_t, 13> pre_relocation_header_words{};
    std::uint32_t allocation_alignment = 0;
    std::uint32_t allocation_alignment_function = 0;
    std::uint32_t allocation_relocation_function = 0;
    std::uint32_t relocated_header_word_index = 0;
    std::array<std::uint32_t, 4> trailing_create_arguments{};
    std::uint32_t owner_wrapper_field_offset = 0;
    std::uint32_t owner_resource_field_offset = 0;
};

// Bind inputs consumed by BgMap_BindForRender.  The source texture is owned
// by the landscape/background-map asset; the generated texture is the exact
// 64x64 FMT_8 target described above.
struct CloudBindRecipe {
    std::uint32_t bind_function = 0;
    std::uint32_t source_hardware_slot = 0;
    std::uint32_t source_resource_field_offset = 0;
    std::uint32_t source_bind_flags = 0;
    std::uint32_t generated_hardware_slot = 0;
    std::uint32_t generated_resource_field_offset = 0;
    std::uint32_t generated_bind_flags = 0;
};

const GeneratedTextureRecipe& ExactGeneratedTextureRecipe() noexcept;
const CloudBindRecipe& ExactCloudBindRecipe() noexcept;

// Exact alignment performed by sub_8225E300 before relocation.
std::uint32_t AlignGeneratedAllocation(
    std::uint32_t current_allocation_address) noexcept;

// Applies the same DWORD-8 relocation as sub_8225FCD8/sub_8225FDB0 to the
// already-aligned allocation base and returns the six binder source words.
std::array<std::uint32_t, 6> BuildGeneratedDescriptorWords(
    std::uint32_t aligned_allocation_base) noexcept;

}  // namespace CloudBackgroundMap
