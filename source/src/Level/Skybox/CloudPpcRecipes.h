#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// Symbolic metadata recovered from default.xex.  Nothing in this namespace
// evaluates a PowerPC or VMX operation with host floating-point arithmetic.
namespace CloudPpcRecipes {

enum class Opcode : std::uint16_t {
    Ld,
    Lwz,
    Lfs,
    Lfsx,
    Lfd,
    Std,
    Stw,
    Stfs,
    Stfd,
    Li,
    Fsub,
    Fsubs,
    Fadds,
    Fmuls,
    Frsp,
    Fctidz,
    Fcfid,
    Fabs,
    Fneg,
    Fsel,
    Fcmpu,
    Mfcr,
    Rlwinm,
    Or,
    Subf,
    Clrrwi,
    Cmpwi,
    Cmplwi,
    Branch,
    BranchEqual,
    BranchNotEqual,
    Call,
    Lvx128,
    Lvlx,
    Vmr,
    Vspltw,
    Vmulfp128,
    Vsubfp,
    Vrlimi128,
    Stvx128,
    SetFloatLogical,
    SetFloat4Logical,
    Return,
};

enum class Symbol : std::uint16_t {
    None,
    AuthoredVelocityX,
    AuthoredVelocityY,
    VelocityX,
    VelocityY,
    OldVelocityPair,
    OldVelocityX,
    OldVelocityY,
    OldUpdateTime,
    CurrentTime,
    DeltaDouble,
    DeltaSingle,
    OldUvX,
    OldUvY,
    StepX,
    StepY,
    SumX,
    SumY,
    FloorXDouble,
    FloorXSingle,
    FloorYDouble,
    FloorYSingle,
    WrappedX,
    WrappedY,
    FloorInput,
    FloorIntegerBits,
    FloorAbsolute,
    FloorOne,
    FloorLimit,
    FloorTruncated,
    FloorLimitMinusAbsolute,
    FloorNegativeAbsolute,
    FloorRemainder,
    FloorTruncatedMinusOne,
    FloorCandidate,
    FloorLimited,
    FloorResult,
    AuthoredNormal,
    NormalLowerBound,
    NormalUpperBound,
    NormalOne,
    NormalDouble,
    NormalSingle,
    NormalLowerDifference,
    PositiveZero,
    LowerCrWord,
    LowerLtOffset,
    LowerUnorderedOffset,
    LowerTableOffset,
    LowerSelector,
    NormalLowered,
    NormalUpperDifference,
    UpperCrWord,
    UpperLtOffset,
    UpperUnorderedOffset,
    UpperTableOffset,
    UpperSelector,
    NormalResult,
    EnvironmentBegin,
    EnvironmentEnd,
    EnvironmentByteCount,
    EnvironmentItem,
    ViewerVector,
    DirectionVector,
    ColourVector,
    LightScaleSingle,
    LightScaleLoad,
    LightScaleLoadedVector,
    LightScaleVector,
    ScaledDirection,
    LightPosition,
    LightZeroVector,
    SizePair,
    SizeX,
    SizeY,
    Height,
    NegativeSizeX,
    NegativeSizeY,
    VertexPack,
    AlphaContext,
    AlphaValue,
    AlphaScale,
    AlphaProduct,
    AlphaIntegerBits,
    AlphaStateValue,
};

enum class OperandKind : std::uint8_t {
    None,
    Symbol,
    LayerOffset,
    ContextOffset,
    EnvironmentItemOffset,
    GlobalAddress,
    RawFloat32,
    RawFloat64,
    ImmediateU32,
    ConditionRegisterField,
    SelectorTable,
    LogicalConstantId,
    VertexWord,
};

struct Operand {
    OperandKind kind = OperandKind::None;
    std::uint64_t value = 0;
    // Component index, signed displacement encoded as u32, or other
    // opcode-specific metadata.  Zero when unused.
    std::uint32_t auxiliary = 0;
};

struct Step {
    std::uint32_t address = 0;
    Opcode opcode = Opcode::Lwz;
    Operand destination{};
    std::array<Operand, 4> operands{};
    std::uint8_t operand_count = 0;
};

struct RecipeView {
    const Step* steps = nullptr;
    std::size_t count = 0;

    const Step* begin() const { return steps; }
    const Step* end() const { return steps + count; }
    const Step& operator[](std::size_t index) const { return steps[index]; }
};

struct RawConstant {
    std::uint32_t address = 0;
    std::uint8_t byte_width = 0;
    // Numeric value of the big-endian word(s) stored in the XEX image.
    std::uint64_t bits = 0;
};

enum class VertexSource : std::uint8_t {
    SizeX,
    SizeY,
    Height,
    PositiveZero,
    PositiveOne,
};

struct VertexWordRecipe {
    std::uint8_t vertex_index = 0;
    std::uint8_t component_index = 0; // x/y/z/u/v
    VertexSource source = VertexSource::PositiveZero;
    bool ppc_fneg = false;
    // Nonzero entries are the scalar LFS/FNEG/STFS provenance.  Pure VMX
    // packing is intentionally represented by the destination word mapping.
    std::array<std::uint32_t, 4> scalar_addresses{};
    std::uint8_t scalar_address_count = 0;
};

const std::array<RawConstant, 13>& Constants();
RecipeView VelocityScale();
RecipeView UvUpdateAndWrap();
RecipeView FloorHelper();
RecipeView NormalStrengthClamp();
RecipeView LightPosition();
RecipeView LightPositionZeroFallback();
RecipeView VertexSignConstruction();
const std::array<VertexWordRecipe, 20>& VertexWords();
RecipeView AlphaReference();

}  // namespace CloudPpcRecipes
