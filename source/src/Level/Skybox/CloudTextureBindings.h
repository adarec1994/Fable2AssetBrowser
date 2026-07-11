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

// One cloud-visible use of the common resource binder at 0x821B7020.
// AssetDescriptor means dimensions/format/mips are supplied by the selected
// authored resource; GeneratedBackground means the static 64x64 FMT_8 recipe
// in CloudBackgroundMap applies.
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
    // Zero means the cloud path does not impose a CPU element width.  The
    // F12 max-hierarchy builder reads level zero as exact 16-bit elements.
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

// Exact raw inputs consumed by 0x821B7020.  descriptor_words map to source
// byte offsets +0x1c,+0x20,+0x24,+0x28,+0x2c,+0x30.  inherited_limit_a/b
// are the bytes read at device+(11950+slot) and device+(11976+slot).
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

// Ordered stores made to the six-DWORD fetch constant.  Word4 is written
// three times by the XEX; all three writes are retained rather than reduced
// to only the final state.
struct FetchConstantMergeTrace {
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
FetchConstantMergeTrace BuildExactMergeTrace(
    const FetchConstantMergeInput& input) noexcept;

// Exact sub_821FAA08/sub_8225FE18 descriptor formulas.  The six words map to
// resource+0x1c through resource+0x30 in order.
DescriptorSummary DecodeDescriptor(
    const std::array<std::uint32_t, 6>& descriptor_words) noexcept;

// Exact sub_821FAC30 mip formula for the valid encoded level range (0..15).
MipDimensions DecodeMipDimensions(
    const std::array<std::uint32_t, 6>& descriptor_words,
    std::uint8_t mip_level) noexcept;

}  // namespace CloudTextureBindings
