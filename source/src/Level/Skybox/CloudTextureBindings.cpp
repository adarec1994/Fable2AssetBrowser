#include "CloudTextureBindings.h"

namespace CloudTextureBindings {

const std::array<BindingRecipe, 3>& ExactCloudBindings() noexcept
{
    static constexpr std::array<BindingRecipe, 3> recipes = {{
        {
            TextureRole::DensityF13,
            13u,
            0x00040000u,
            0x40u,
            0x20u,
            0x24u,
            true,
            true,
            DescriptorProvenance::AssetDescriptor,
            0u,
        },
        {
            TextureRole::BackgroundSourceF12,
            12u,
            0x00080000u,
            0x114u,
            0u,
            0u,
            false,
            false,
            DescriptorProvenance::AssetDescriptor,
            2u,
        },
        {
            TextureRole::BackgroundGeneratedF11,
            11u,
            0x00100000u,
            0x11cu,
            0u,
            0u,
            false,
            false,
            DescriptorProvenance::GeneratedBackground,
            0u,
        },
    }};
    return recipes;
}

const CommonBinderLayout& ExactCommonBinderLayout() noexcept
{
    static constexpr CommonBinderLayout layout = {
        0x821b7020u,
        0x1cu,
        6u,
        0x480u,
        0x18u,
        0x3100u,
        4u,
        0x18u,
        11950u,
        11976u,
    };
    return layout;
}

FetchConstantMergeTrace BuildExactMergeTrace(
    const FetchConstantMergeInput& input) noexcept
{
    const auto& old = input.old_slot_words;
    const auto& source = input.descriptor_words;
    FetchConstantMergeTrace result{};
    result.final_words = old;

    const auto store = [&](std::size_t index, std::uint32_t address,
                           std::uint8_t destination,
                           std::uint32_t value) {
        result.writes[index] = {address, destination, value};
        result.final_words[destination] = value;
    };

    // Store order and formulas are the literal integer operations at
    // 0x821B70AC..0x821B711C.  All arithmetic is unsigned 32-bit arithmetic.
    store(0, 0x821b70acu, 2u, source[2]);
    store(1, 0x821b70c0u, 4u,
          (old[4] & 0xfffffc03u) | (source[4] & 0x000003fcu));
    store(2, 0x821b70c8u, 0u,
          (old[0] & 0x003ffc00u) | (source[0] & 0xffc003ffu));

    const std::uint32_t source1_low = source[1] & 0x1fffffffu;
    const std::uint32_t source1_bit12 =
        ((source[1] >> 20u) + 512u) & 0x00001000u;
    store(3, 0x821b70d0u, 1u,
          (old[1] & 0x00000800u) |
              ((source1_bit12 + source1_low) & 0xfffff7ffu));
    store(4, 0x821b70d8u, 3u,
          (old[3] & 0x7ff80000u) | (source[3] & 0x8007ffffu));

    const std::uint32_t source5_bit12 =
        ((source[5] >> 20u) + 512u) & 0x00001000u;
    const std::uint32_t source5_value =
        source5_bit12 + (source[5] & 0x1ffffe00u);
    store(5, 0x821b70dcu, 5u,
          (old[5] & 0x000001ffu) |
              (source5_value & 0xfffffe00u));

    std::uint32_t limit_a = input.inherited_limit_a;
    const std::uint32_t source_limit_a = (source[4] >> 2u) & 0x0fu;
    if (source_limit_a > limit_a) limit_a = source_limit_a;
    const std::uint32_t word4_after_a =
        ((4u * limit_a) & 0x0000003cu) |
        (old[4] & 0xfffffc03u) |
        (source[4] & 0x000003c0u);
    store(6, 0x821b7100u, 4u, word4_after_a);

    std::uint32_t limit_b = input.inherited_limit_b;
    const std::uint32_t source_limit_b = (source[4] >> 6u) & 0x0fu;
    if (source_limit_b < limit_b) limit_b = source_limit_b;
    store(7, 0x821b711cu, 4u,
          ((limit_b << 6u) & 0x000003c0u) |
              (word4_after_a & 0xfffffc3fu));
    return result;
}

DescriptorSummary DecodeDescriptor(
    const std::array<std::uint32_t, 6>& words) noexcept
{
    DescriptorSummary result{};
    result.data_format = words[1] & 0x3fu;
    result.type = (words[5] >> 9u) & 3u;
    result.dimension_bias = ((words[3] >> 30u) & 2u) + 1u;
    result.level_count = ((words[4] >> 6u) & 0x0fu) + 1u;

    if (result.type == 0u) {
        result.width = (words[2] & 0x00ffffffu) +
            result.dimension_bias;
        result.height = 1u;
        result.depth = 1u;
    } else if (result.type == 2u) {
        result.width = (words[2] & 0x000007ffu) +
            result.dimension_bias;
        result.height = ((words[2] >> 11u) & 0x000007ffu) +
            result.dimension_bias;
        result.depth = (words[2] >> 22u) + result.dimension_bias;
    } else {
        result.width = (words[2] & 0x00001fffu) +
            result.dimension_bias;
        result.height = ((words[2] >> 13u) & 0x00001fffu) +
            result.dimension_bias;
        result.depth = 1u;
    }
    return result;
}

MipDimensions DecodeMipDimensions(
    const std::array<std::uint32_t, 6>& words,
    std::uint8_t mip_level) noexcept
{
    const DescriptorSummary descriptor = DecodeDescriptor(words);
    if (mip_level == 0u) {
        return {descriptor.width, descriptor.height, descriptor.depth};
    }

    const std::uint32_t border = 2u * (words[3] >> 31u);
    const auto reduce = [&](std::uint32_t dimension) {
        const std::uint32_t shifted =
            (dimension - border) >> mip_level;
        return (shifted > 1u ? shifted : 1u) + border;
    };
    return {
        reduce(descriptor.width),
        reduce(descriptor.height),
        descriptor.type == 2u ? reduce(descriptor.depth) : 1u,
    };
}

}  // namespace CloudTextureBindings
