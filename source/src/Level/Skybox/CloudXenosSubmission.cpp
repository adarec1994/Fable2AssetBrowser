#include "CloudXenosSubmission.h"

namespace CloudXenosSubmission {

const SubmissionRecipe& ExactSubmissionRecipe() noexcept
{
    static constexpr SubmissionRecipe recipe = {
        RetailExecutionBackend::DirectXenosCommandBuffer,
        false,
        false,
        0x8223a1e8u,
        0x8222c268u,
        0x82208c48u,
        0x82b843f8u,
        0x82b84550u,
        0x82b92c50u,
        0x8220a528u,
        0x82217ee8u,
        0x8221b140u,
        {{0x8221b328u, 0x8221b358u, 0x8221b36cu}},
        {{
            0x8221b7c8u, 0x8221b7fcu, 0x8221b814u,
            0x8221b8c4u, 0x8221b8f4u, 0x8221b908u,
        }},
        0x822181dcu,
        0x822183a4u,
        0x8221c908u,
        0x82b9ba58u,
        {{0x82b9bc58u, 0x82b9bc90u, 0x82b9bd48u}},
        0x82b9cd68u,
        {{0x82b9cf68u, 0x82b9cfccu}},
    };
    return recipe;
}

}  // namespace CloudXenosSubmission
