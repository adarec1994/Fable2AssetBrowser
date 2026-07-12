#include "CloudBackgroundMap.h"

namespace CloudBackgroundMap {

const GeneratedTextureRecipe& ExactGeneratedTextureRecipe() noexcept
{

    static constexpr GeneratedTextureRecipe recipe = {
        0x82a79330u,
        0x82a7a514u,
        0x821fbc60u,
        0x100u,
        0x104u,
        64u,
        64u,
        1u,
        1u,
        0x8331d988u,
        0x70u,
        23u,
        0x8331e398u,
        {{
            0x28000102u, 0x00000001u, 0x00000008u, 0x00000000u,
            0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
            0x00000008u, 0x00000000u, 0x00000000u, 0x00000000u,
            0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
            0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
            0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
            0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
        }},
        0x3fu,
        2u,
        0x820ff844u,
        {{'F', 'M', 'T', '_', '8', '\0'}},
        1u,
        1u,
        8u,
        1u,
        64u,
        64u,
        64u,
        4096u,
        0u,
        {{
            0x00000003u,
            0x00000001u,
            0x00000000u,
            0x00000000u,
            0x00000000u,
            0xffff0000u,
            0xffff0000u,
            0x80800002u,
            0x00000002u,
            0x0007e03fu,
            0x00001400u,
            0x00000000u,
            0x00000200u,
        }},
        4096u,
        0x8225e300u,
        0x8225fcd8u,
        8u,
        {{23u, 2u, 0u, 1u}},
        0x118u,
        0x11cu,
    };
    return recipe;
}

std::uint32_t AlignGeneratedAllocation(
    std::uint32_t current_allocation_address) noexcept
{
    return (current_allocation_address + 4095u) & 0xfffff000u;
}

std::array<std::uint32_t, 6> BuildGeneratedDescriptorWords(
    std::uint32_t aligned_allocation_base) noexcept
{
    const GeneratedTextureRecipe& recipe = ExactGeneratedTextureRecipe();
    std::array<std::uint32_t, 13> header =
        recipe.pre_relocation_header_words;
    std::uint32_t& relocated =
        header[recipe.relocated_header_word_index];
    relocated = (relocated & 0x00000fffu) |
        (((relocated & 0xfffff000u) + aligned_allocation_base) &
         0xfffff000u);
    return {{
        header[7], header[8], header[9],
        header[10], header[11], header[12],
    }};
}

const CloudBindRecipe& ExactCloudBindRecipe() noexcept
{
    static constexpr CloudBindRecipe recipe = {
        0x82a7a8a8u,
        12u,
        0x114u,
        0x00080000u,
        11u,
        0x11cu,
        0x00100000u,
    };
    return recipe;
}

}
