#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace CloudTextureBindings {

enum class TextureRole : std::uint8_t {
    DensityF13,
    BackgroundSourceF12,
    BackgroundGeneratedF11,
};

enum class DescriptorProvenance : std::uint8_t {
    AssetDescriptor,
    GeneratedBackground,
};

struct BindingRecipe {
    TextureRole role = TextureRole::DensityF13;
    std::uint32_t hardware_slot = 0;
    std::uint32_t bind_flags = 0;
    std::uint32_t owner_resource_field_offset = 0;
    std::uint32_t prepare_result_byte_offset = 0;
    std::uint32_t prepared_payload_field_byte_offset = 0;
    bool resource_is_prepare_result_plus4 = false;
    bool prepare_each_cloud_draw = false;
    DescriptorProvenance descriptor_provenance =
        DescriptorProvenance::AssetDescriptor;

    std::uint32_t cpu_level_zero_element_bytes = 0;
};

struct DescriptorSummary {
    std::uint32_t data_format = 0;
    std::uint32_t type = 0;
    std::uint32_t dimension_bias = 0;
    std::uint32_t level_count = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t depth = 0;
};

struct MipDimensions {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t depth = 0;
};

struct FetchConstantMergeInput {
    std::array<std::uint32_t, 6> old_slot_words{};
    std::array<std::uint32_t, 6> descriptor_words{};
    std::uint8_t inherited_limit_a = 0;
    std::uint8_t inherited_limit_b = 0;
};

struct FetchConstantWordWrite {
    std::uint32_t instruction_address = 0;
    std::uint8_t destination_word = 0;
    std::uint32_t value = 0;
};

struct FetchConstantMergeResult {
    std::array<FetchConstantWordWrite, 8> writes{};
    std::array<std::uint32_t, 6> final_words{};
};

struct CommonBinderLayout {
    std::uint32_t function_address = 0;
    std::uint32_t descriptor_first_word_offset = 0;
    std::uint32_t descriptor_word_count = 0;
    std::uint32_t fetch_constant_base_offset = 0;
    std::uint32_t fetch_constant_stride = 0;
    std::uint32_t resource_cache_base_offset = 0;
    std::uint32_t resource_cache_stride = 0;
    std::uint32_t dirty_qword_offset = 0;
    std::uint32_t inherited_limit_a_base_offset = 0;
    std::uint32_t inherited_limit_b_base_offset = 0;
};

const std::array<BindingRecipe, 3>& ExactCloudBindings() noexcept;
const CommonBinderLayout& ExactCommonBinderLayout() noexcept;
FetchConstantMergeResult BuildExactMergeResult(
    const FetchConstantMergeInput& input) noexcept;
DescriptorSummary DecodeDescriptor(
    const std::array<std::uint32_t, 6>& descriptor_words) noexcept;

MipDimensions DecodeMipDimensions(
    const std::array<std::uint32_t, 6>& descriptor_words,
    std::uint8_t mip_level) noexcept;

}
