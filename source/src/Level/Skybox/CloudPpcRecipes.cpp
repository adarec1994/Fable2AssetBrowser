#include "CloudPpcRecipes.h"

namespace CloudPpcRecipes {
namespace {

constexpr Operand None() { return {}; }
constexpr Operand Sym(Symbol value, std::uint32_t auxiliary = 0)
{
    return {OperandKind::Symbol, static_cast<std::uint64_t>(value), auxiliary};
}
constexpr Operand Layer(std::uint32_t offset)
{
    return {OperandKind::LayerOffset, offset, 0};
}
constexpr Operand Context(std::uint32_t offset)
{
    return {OperandKind::ContextOffset, offset, 0};
}
constexpr Operand Item(std::uint32_t offset)
{
    return {OperandKind::EnvironmentItemOffset, offset, 0};
}
constexpr Operand Global(std::uint32_t address,
                         std::uint32_t auxiliary = 0)
{
    return {OperandKind::GlobalAddress, address, auxiliary};
}
constexpr Operand F32(std::uint32_t bits)
{
    return {OperandKind::RawFloat32, bits, 0};
}
constexpr Operand F64(std::uint64_t bits)
{
    return {OperandKind::RawFloat64, bits, 0};
}
constexpr Operand Imm(std::uint32_t value)
{
    return {OperandKind::ImmediateU32, value, 0};
}
constexpr Operand Cr(std::uint32_t field)
{
    return {OperandKind::ConditionRegisterField, field, 0};
}
constexpr Operand Table(std::uint32_t address)
{
    return {OperandKind::SelectorTable, address, 0};
}
constexpr Operand Logical(std::uint32_t id)
{
    return {OperandKind::LogicalConstantId, id, 0};
}

constexpr Step S0(std::uint32_t address, Opcode opcode, Operand destination)
{
    return {address, opcode, destination, {}, 0};
}
constexpr Step S1(std::uint32_t address, Opcode opcode, Operand destination,
                  Operand a)
{
    return {address, opcode, destination, {{a, None(), None(), None()}}, 1};
}
constexpr Step S2(std::uint32_t address, Opcode opcode, Operand destination,
                  Operand a, Operand b)
{
    return {address, opcode, destination, {{a, b, None(), None()}}, 2};
}
constexpr Step S3(std::uint32_t address, Opcode opcode, Operand destination,
                  Operand a, Operand b, Operand c)
{
    return {address, opcode, destination, {{a, b, c, None()}}, 3};
}
constexpr Step S4(std::uint32_t address, Opcode opcode, Operand destination,
                  Operand a, Operand b, Operand c, Operand d)
{
    return {address, opcode, destination, {{a, b, c, d}}, 4};
}

template <std::size_t N>
RecipeView View(const std::array<Step, N>& steps)
{
    return {steps.data(), steps.size()};
}

constexpr std::array<RawConstant, 13> kConstants = {{
    {0x820994B4, 4, 0x00000000ULL},
    {0x820994C0, 4, 0x3F800000ULL},
    {0x8209952C, 4, 0x3A83126FULL},
    {0x8209BF50, 4, 0x3E19999AULL},
    {0x8209B650, 4, 0x3DCCCCCDULL},
    {0x8209B4E0, 4, 0x40000000ULL},
    {0x8209BADC, 4, 0x437F0000ULL},
    {0x82099580, 4, 0x44FA0000ULL},
    {0x8209FFD8, 8, 0x3FF0000000000000ULL},
    {0x82000CB0, 8, 0x3FF0000000000000ULL},
    {0x82000CD8, 8, 0x43ABC16D674EC800ULL},
    {0x82100170, 4, 0x3F800000ULL},
    {0x82100174, 4, 0xBF800000ULL},
}};

constexpr std::array<Step, 2> kVelocityScale = {{
    S2(0x82267724, Opcode::Fmuls, Sym(Symbol::VelocityX),
       Sym(Symbol::AuthoredVelocityX), F32(0x3A83126F)),
    S2(0x8226773C, Opcode::Fmuls, Sym(Symbol::VelocityY),
       Sym(Symbol::AuthoredVelocityY), F32(0x3A83126F)),
}};

constexpr std::array<Step, 28> kUvUpdateAndWrap = {{
    S1(0x82267780, Opcode::Ld, Sym(Symbol::OldVelocityPair), Layer(0x34)),
    S1(0x8226778C, Opcode::Lfd, Sym(Symbol::OldUpdateTime), Layer(0x48)),
    S1(0x82267790, Opcode::Lfd, Sym(Symbol::CurrentTime), Global(0x83496C48)),
    S1(0x82267794, Opcode::Stfd, Layer(0x48), Sym(Symbol::CurrentTime)),
    S2(0x82267798, Opcode::Fsub, Sym(Symbol::DeltaDouble),
       Sym(Symbol::CurrentTime), Sym(Symbol::OldUpdateTime)),
    S1(0x8226779C, Opcode::Lfs, Sym(Symbol::OldUvX), Layer(0x50)),
    S1(0x822677A0, Opcode::Lfs, Sym(Symbol::OldUvY), Layer(0x54)),
    S1(0x822677A4, Opcode::Frsp, Sym(Symbol::DeltaSingle),
       Sym(Symbol::DeltaDouble)),
    S1(0x822677AC, Opcode::Std, Sym(Symbol::OldVelocityPair),
       Sym(Symbol::OldVelocityPair)),
    S1(0x822677B0, Opcode::Lfs, Sym(Symbol::OldVelocityY),
       Sym(Symbol::OldVelocityPair, 1)),
    S1(0x822677B4, Opcode::Lfs, Sym(Symbol::OldVelocityX),
       Sym(Symbol::OldVelocityPair, 0)),
    S2(0x822677BC, Opcode::Fmuls, Sym(Symbol::StepX),
       Sym(Symbol::OldVelocityX), Sym(Symbol::DeltaSingle)),
    S2(0x822677C0, Opcode::Fmuls, Sym(Symbol::StepY),
       Sym(Symbol::OldVelocityY), Sym(Symbol::DeltaSingle)),
    S2(0x822677C4, Opcode::Fadds, Sym(Symbol::SumX),
       Sym(Symbol::StepX), Sym(Symbol::OldUvX)),
    S1(0x822677C8, Opcode::Stfs, Layer(0x50), Sym(Symbol::SumX)),
    S2(0x822677CC, Opcode::Fadds, Sym(Symbol::SumY),
       Sym(Symbol::OldUvY), Sym(Symbol::StepY)),
    S1(0x822677D0, Opcode::Stfs, Layer(0x54), Sym(Symbol::SumY)),
    S2(0x822677D4, Opcode::Call, Sym(Symbol::FloorXDouble),
       Global(0x8222C3E8), Sym(Symbol::SumX)),
    S1(0x822677D8, Opcode::Frsp, Sym(Symbol::FloorXSingle),
       Sym(Symbol::FloorXDouble)),
    S1(0x822677DC, Opcode::Lfs, Sym(Symbol::SumX), Layer(0x50)),
    S1(0x822677E0, Opcode::Lfs, Sym(Symbol::SumY), Layer(0x54)),
    S2(0x822677E4, Opcode::Fsubs, Sym(Symbol::WrappedX),
       Sym(Symbol::SumX), Sym(Symbol::FloorXSingle)),
    S1(0x822677E8, Opcode::Stfs, Layer(0x50), Sym(Symbol::WrappedX)),
    S2(0x822677EC, Opcode::Call, Sym(Symbol::FloorYDouble),
       Global(0x8222C3E8), Sym(Symbol::SumY)),
    S1(0x822677F0, Opcode::Frsp, Sym(Symbol::FloorYSingle),
       Sym(Symbol::FloorYDouble)),
    S1(0x822677F4, Opcode::Lfs, Sym(Symbol::SumY), Layer(0x54)),
    S2(0x82267800, Opcode::Fsubs, Sym(Symbol::WrappedY),
       Sym(Symbol::SumY), Sym(Symbol::FloorYSingle)),
    S1(0x82267804, Opcode::Stfs, Layer(0x54), Sym(Symbol::WrappedY)),
}};

constexpr std::array<Step, 13> kFloorHelper = {{
    S1(0x8222C3E8, Opcode::Fctidz, Sym(Symbol::FloorIntegerBits),
       Sym(Symbol::FloorInput)),
    S1(0x8222C3F0, Opcode::Fabs, Sym(Symbol::FloorAbsolute),
       Sym(Symbol::FloorInput)),
    S1(0x8222C3F8, Opcode::Lfd, Sym(Symbol::FloorOne), Global(0x82000CB0)),
    S1(0x8222C3FC, Opcode::Lfd, Sym(Symbol::FloorLimit), Global(0x82000CD8)),
    S1(0x8222C400, Opcode::Fcfid, Sym(Symbol::FloorTruncated),
       Sym(Symbol::FloorIntegerBits)),
    S2(0x8222C404, Opcode::Fsub, Sym(Symbol::FloorLimitMinusAbsolute),
       Sym(Symbol::FloorLimit), Sym(Symbol::FloorAbsolute)),
    S1(0x8222C408, Opcode::Fneg, Sym(Symbol::FloorNegativeAbsolute),
       Sym(Symbol::FloorAbsolute)),
    S2(0x8222C40C, Opcode::Fsub, Sym(Symbol::FloorRemainder),
       Sym(Symbol::FloorInput), Sym(Symbol::FloorTruncated)),
    S2(0x8222C410, Opcode::Fsub, Sym(Symbol::FloorTruncatedMinusOne),
       Sym(Symbol::FloorTruncated), Sym(Symbol::FloorOne)),
    S3(0x8222C414, Opcode::Fsel, Sym(Symbol::FloorCandidate),
       Sym(Symbol::FloorRemainder), Sym(Symbol::FloorTruncated),
       Sym(Symbol::FloorTruncatedMinusOne)),
    S3(0x8222C418, Opcode::Fsel, Sym(Symbol::FloorLimited),
       Sym(Symbol::FloorLimitMinusAbsolute), Sym(Symbol::FloorCandidate),
       Sym(Symbol::FloorInput)),
    S3(0x8222C41C, Opcode::Fsel, Sym(Symbol::FloorResult),
       Sym(Symbol::FloorNegativeAbsolute), Sym(Symbol::FloorInput),
       Sym(Symbol::FloorLimited)),
    S0(0x8222C420, Opcode::Return, Sym(Symbol::FloorResult)),
}};

constexpr std::array<Step, 28> kNormalStrengthClamp = {{
    S1(0x8223A3C8, Opcode::Lfs, Sym(Symbol::AuthoredNormal), Layer(0x08)),
    S1(0x8223A3E8, Opcode::Lwz, Sym(Symbol::NormalOne, 0), Global(0x8209FFD8)),
    S1(0x8223A3EC, Opcode::Lfs, Sym(Symbol::NormalLowerBound),
       Global(0x8209B650)),
    S1(0x8223A3F0, Opcode::Lwz, Sym(Symbol::NormalOne, 1), Global(0x8209FFDC)),
    S1(0x8223A3F8, Opcode::Lfs, Sym(Symbol::PositiveZero), Global(0x820994B4)),
    S1(0x8223A410, Opcode::Stw, Sym(Symbol::NormalOne, 0), Sym(Symbol::NormalOne, 0)),
    S1(0x8223A418, Opcode::Stw, Sym(Symbol::NormalOne, 1), Sym(Symbol::NormalOne, 1)),
    S1(0x8223A428, Opcode::Lfd, Sym(Symbol::NormalOne), Global(0x8209FFD8)),
    S2(0x8223A42C, Opcode::Fsub, Sym(Symbol::NormalDouble),
       Sym(Symbol::NormalOne), Sym(Symbol::AuthoredNormal)),
    S1(0x8223A430, Opcode::Frsp, Sym(Symbol::NormalSingle),
       Sym(Symbol::NormalDouble)),
    S2(0x8223A438, Opcode::Fsubs, Sym(Symbol::NormalLowerDifference),
       Sym(Symbol::NormalSingle), F32(0x3DCCCCCD)),
    S2(0x8223A43C, Opcode::Fcmpu, Cr(6),
       Sym(Symbol::NormalLowerDifference), Sym(Symbol::PositiveZero)),
    S1(0x8223A440, Opcode::Mfcr, Sym(Symbol::LowerCrWord), Cr(6)),
    S4(0x8223A444, Opcode::Rlwinm, Sym(Symbol::LowerLtOffset),
       Sym(Symbol::LowerCrWord), Imm(27), Imm(29), Imm(29)),
    S4(0x8223A448, Opcode::Rlwinm, Sym(Symbol::LowerUnorderedOffset),
       Sym(Symbol::LowerCrWord), Imm(30), Imm(29), Imm(29)),
    S2(0x8223A44C, Opcode::Or, Sym(Symbol::LowerTableOffset),
       Sym(Symbol::LowerLtOffset), Sym(Symbol::LowerUnorderedOffset)),
    S2(0x8223A450, Opcode::Lfsx, Sym(Symbol::LowerSelector),
       Table(0x82100170), Sym(Symbol::LowerTableOffset)),
    S3(0x8223A454, Opcode::Fsel, Sym(Symbol::NormalLowered),
       Sym(Symbol::LowerSelector), Sym(Symbol::NormalSingle),
       Sym(Symbol::NormalLowerBound)),
    S1(0x8223A4A4, Opcode::Lfs, Sym(Symbol::NormalUpperBound),
       Global(0x8209B4E0)),
    S2(0x8223A4AC, Opcode::Fsubs, Sym(Symbol::NormalUpperDifference),
       Sym(Symbol::NormalLowered), Sym(Symbol::NormalUpperBound)),
    S2(0x8223A4B0, Opcode::Fcmpu, Cr(6),
       Sym(Symbol::NormalUpperDifference), Sym(Symbol::PositiveZero)),
    S1(0x8223A4B4, Opcode::Mfcr, Sym(Symbol::UpperCrWord), Cr(6)),
    S4(0x8223A4B8, Opcode::Rlwinm, Sym(Symbol::UpperLtOffset),
       Sym(Symbol::UpperCrWord), Imm(27), Imm(29), Imm(29)),
    S4(0x8223A4BC, Opcode::Rlwinm, Sym(Symbol::UpperUnorderedOffset),
       Sym(Symbol::UpperCrWord), Imm(30), Imm(29), Imm(29)),
    S2(0x8223A4C0, Opcode::Or, Sym(Symbol::UpperTableOffset),
       Sym(Symbol::UpperLtOffset), Sym(Symbol::UpperUnorderedOffset)),
    S2(0x8223A4C4, Opcode::Lfsx, Sym(Symbol::UpperSelector),
       Table(0x82100170), Sym(Symbol::UpperTableOffset)),
    S3(0x8223A4C8, Opcode::Fsel, Sym(Symbol::NormalResult),
       Sym(Symbol::UpperSelector), Sym(Symbol::NormalUpperBound),
       Sym(Symbol::NormalLowered)),
    S1(0x8223A4CC, Opcode::SetFloatLogical, Logical(157),
       Sym(Symbol::NormalResult)),
}};

constexpr std::array<Step, 23> kLightPosition = {{
    S1(0x8223A4F0, Opcode::Lwz, Sym(Symbol::EnvironmentBegin), Global(0x834A4B18)),
    S1(0x8223A4F4, Opcode::Lwz, Sym(Symbol::EnvironmentEnd), Global(0x834A4B1C)),
    S2(0x8223A4F8, Opcode::Subf, Sym(Symbol::EnvironmentByteCount),
       Sym(Symbol::EnvironmentBegin), Sym(Symbol::EnvironmentEnd)),
    S2(0x8223A4FC, Opcode::Clrrwi, Sym(Symbol::EnvironmentByteCount),
       Sym(Symbol::EnvironmentByteCount), Imm(2)),
    S2(0x8223A500, Opcode::Cmpwi, Cr(6),
       Sym(Symbol::EnvironmentByteCount), Imm(0)),
    S1(0x8223A504, Opcode::BranchEqual, Global(0x8223A56C), Cr(6)),
    S1(0x8223A508, Opcode::Lwz, Sym(Symbol::EnvironmentItem),
       Sym(Symbol::EnvironmentEnd, 0xFFFFFFFCu)),
    S2(0x8223A514, Opcode::Cmplwi, Cr(6), Sym(Symbol::EnvironmentItem), Imm(0)),
    S1(0x8223A518, Opcode::BranchEqual, Global(0x8223A56C), Cr(6)),
    S1(0x8223A520, Opcode::Lfs, Sym(Symbol::LightScaleSingle), Global(0x82099580)),
    S1(0x8223A528, Opcode::Lvx128, Sym(Symbol::ViewerVector), Context(0x690)),
    S1(0x8223A52C, Opcode::Stfs, Sym(Symbol::LightScaleLoad),
       Sym(Symbol::LightScaleSingle)),
    S1(0x8223A530, Opcode::Lvx128, Sym(Symbol::DirectionVector), Item(0x130)),
    S1(0x8223A534, Opcode::Vmr, Sym(Symbol::DirectionVector),
       Sym(Symbol::DirectionVector)),
    S1(0x8223A538, Opcode::SetFloat4Logical, Logical(126),
       Sym(Symbol::DirectionVector)),
    S1(0x8223A544, Opcode::Lvx128, Sym(Symbol::ColourVector), Item(0x0D0)),
    S1(0x8223A548, Opcode::SetFloat4Logical, Logical(122),
       Sym(Symbol::ColourVector)),
    S1(0x8223A554, Opcode::Lvlx, Sym(Symbol::LightScaleLoadedVector),
       Sym(Symbol::LightScaleLoad)),
    S2(0x8223A558, Opcode::Vspltw, Sym(Symbol::LightScaleVector),
       Sym(Symbol::LightScaleLoadedVector), Imm(0)),
    S2(0x8223A55C, Opcode::Vmulfp128, Sym(Symbol::ScaledDirection),
       Sym(Symbol::DirectionVector), Sym(Symbol::LightScaleVector)),
    S2(0x8223A560, Opcode::Vsubfp, Sym(Symbol::LightPosition),
       Sym(Symbol::ViewerVector), Sym(Symbol::ScaledDirection)),
    S1(0x8223A564, Opcode::SetFloat4Logical, Logical(131),
       Sym(Symbol::LightPosition)),
    S1(0x8223A568, Opcode::Branch, Global(0x8223A644), Imm(0)),
}};

constexpr std::array<Step, 14> kLightPositionZero = {{
    S1(0x8223A570, Opcode::Stfs, Sym(Symbol::LightZeroVector), F32(0)),
    S1(0x8223A578, Opcode::Stfs, Sym(Symbol::LightZeroVector), F32(0)),
    S1(0x8223A580, Opcode::Stfs, Sym(Symbol::LightZeroVector), F32(0)),
    S1(0x8223A588, Opcode::Stfs, Sym(Symbol::LightZeroVector), F32(0)),
    S1(0x8223A590, Opcode::Lvlx, Sym(Symbol::LightZeroVector), F32(0)),
    S2(0x8223A5A0, Opcode::Vrlimi128, Sym(Symbol::LightZeroVector),
       Sym(Symbol::LightZeroVector), Imm(0x0403)),
    S1(0x8223A5B0, Opcode::SetFloat4Logical, Logical(126),
       Sym(Symbol::LightZeroVector)),
    S1(0x8223A5B8, Opcode::Stfs, Sym(Symbol::LightZeroVector), F32(0)),
    S1(0x8223A5CC, Opcode::Stfs, Sym(Symbol::LightZeroVector), F32(0)),
    S1(0x8223A5F8, Opcode::SetFloat4Logical, Logical(122),
       Sym(Symbol::LightZeroVector)),
    S1(0x8223A604, Opcode::Stfs, Sym(Symbol::LightZeroVector), F32(0)),
    S1(0x8223A614, Opcode::Stfs, Sym(Symbol::LightZeroVector), F32(0)),
    S2(0x8223A630, Opcode::Vrlimi128, Sym(Symbol::LightZeroVector),
       Sym(Symbol::LightZeroVector), Imm(0x0403)),
    S1(0x8223A640, Opcode::SetFloat4Logical, Logical(131),
       Sym(Symbol::LightZeroVector)),
}};

constexpr std::array<Step, 12> kVertexSignConstruction = {{
    S1(0x8223AED8, Opcode::Ld, Sym(Symbol::SizePair), Layer(0x24)),
    S1(0x8223AEE8, Opcode::Lfs, Sym(Symbol::Height), Layer(0x04)),
    S1(0x8223AEF0, Opcode::Stfs, Sym(Symbol::VertexPack), Sym(Symbol::Height)),
    S1(0x8223AF00, Opcode::Std, Sym(Symbol::VertexPack), Sym(Symbol::SizePair)),
    S1(0x8223AF04, Opcode::Lfs, Sym(Symbol::SizeX), Sym(Symbol::SizePair, 0)),
    S1(0x8223AF70, Opcode::Lfs, Sym(Symbol::SizeY), Sym(Symbol::SizePair, 1)),
    S1(0x8223AF74, Opcode::Fneg, Sym(Symbol::NegativeSizeX), Sym(Symbol::SizeX)),
    S1(0x8223AF84, Opcode::Fneg, Sym(Symbol::NegativeSizeY), Sym(Symbol::SizeY)),
    S1(0x8223AF90, Opcode::Stfs, Sym(Symbol::VertexPack), Sym(Symbol::NegativeSizeY)),
    S1(0x8223AFB0, Opcode::Stfs, Sym(Symbol::VertexPack), Sym(Symbol::NegativeSizeX)),
    S1(0x8223AFC0, Opcode::Stfs, Sym(Symbol::VertexPack), Sym(Symbol::NegativeSizeX)),
    S1(0x8223AFD8, Opcode::Stfs, Sym(Symbol::VertexPack), Sym(Symbol::NegativeSizeY)),
}};

constexpr VertexWordRecipe V(std::uint8_t vertex, std::uint8_t component,
                             VertexSource source, bool negate,
                             std::array<std::uint32_t, 4> addresses = {},
                             std::uint8_t count = 0)
{
    return {vertex, component, source, negate, addresses, count};
}

constexpr std::array<VertexWordRecipe, 20> kVertexWords = {{
    V(0, 0, VertexSource::SizeX, true, {{0x8223AF04, 0x8223AF74, 0x8223AFB0, 0}}, 3),
    V(0, 1, VertexSource::SizeY, true, {{0x8223AF70, 0x8223AF84, 0x8223AF90, 0}}, 3),
    V(0, 2, VertexSource::Height, false, {{0x8223AEE8, 0x8223AEF0, 0, 0}}, 2),
    V(0, 3, VertexSource::PositiveZero, false),
    V(0, 4, VertexSource::PositiveZero, false),
    V(1, 0, VertexSource::SizeX, false),
    V(1, 1, VertexSource::SizeY, true, {{0x8223AF70, 0x8223AF84, 0x8223AF90, 0}}, 3),
    V(1, 2, VertexSource::Height, false, {{0x8223AEE8, 0x8223AEF0, 0, 0}}, 2),
    V(1, 3, VertexSource::PositiveOne, false),
    V(1, 4, VertexSource::PositiveZero, false),
    V(2, 0, VertexSource::SizeX, true, {{0x8223AF04, 0x8223AF74, 0x8223AFB0, 0}}, 3),
    V(2, 1, VertexSource::SizeY, false),
    V(2, 2, VertexSource::Height, false, {{0x8223AEE8, 0x8223AEF0, 0, 0}}, 2),
    V(2, 3, VertexSource::PositiveZero, false),
    V(2, 4, VertexSource::PositiveOne, false),
    V(3, 0, VertexSource::SizeX, false),
    V(3, 1, VertexSource::SizeY, false),
    V(3, 2, VertexSource::Height, false, {{0x8223AEE8, 0x8223AEF0, 0, 0}}, 2),
    V(3, 3, VertexSource::PositiveOne, false),
    V(3, 4, VertexSource::PositiveOne, false),
}};

constexpr std::array<Step, 11> kAlphaReference = {{
    S1(0x8223AA78, Opcode::Lwz, Sym(Symbol::AlphaContext), Context(0x6FC)),
    S2(0x8223AA7C, Opcode::Cmpwi, Cr(6), Sym(Symbol::AlphaContext), Imm(2)),
    S1(0x8223AA80, Opcode::BranchNotEqual, Global(0x8223AA8C), Cr(6)),
    S1(0x8223AA84, Opcode::Li, Sym(Symbol::AlphaStateValue), Imm(5)),
    S1(0x8223AA88, Opcode::Branch, Global(0x8223AAA4), Imm(0)),
    S1(0x8223AA8C, Opcode::Lfs, Sym(Symbol::AlphaValue), Layer(0x10)),
    S1(0x8223AA90, Opcode::Lfs, Sym(Symbol::AlphaScale), Global(0x8209BADC)),
    S2(0x8223AA94, Opcode::Fmuls, Sym(Symbol::AlphaProduct),
       Sym(Symbol::AlphaValue), Sym(Symbol::AlphaScale)),
    S1(0x8223AA98, Opcode::Fctidz, Sym(Symbol::AlphaIntegerBits),
       Sym(Symbol::AlphaProduct)),
    S1(0x8223AA9C, Opcode::Stfd, Sym(Symbol::AlphaIntegerBits),
       Sym(Symbol::AlphaIntegerBits)),
    S1(0x8223AAA0, Opcode::Lwz, Sym(Symbol::AlphaStateValue),
       Sym(Symbol::AlphaIntegerBits, 4)),
}};

}  // namespace

const std::array<RawConstant, 13>& Constants() { return kConstants; }
RecipeView VelocityScale() { return View(kVelocityScale); }
RecipeView UvUpdateAndWrap() { return View(kUvUpdateAndWrap); }
RecipeView FloorHelper() { return View(kFloorHelper); }
RecipeView NormalStrengthClamp() { return View(kNormalStrengthClamp); }
RecipeView LightPosition() { return View(kLightPosition); }
RecipeView LightPositionZeroFallback() { return View(kLightPositionZero); }
RecipeView VertexSignConstruction() { return View(kVertexSignConstruction); }
const std::array<VertexWordRecipe, 20>& VertexWords() { return kVertexWords; }
RecipeView AlphaReference() { return View(kAlphaReference); }

}  // namespace CloudPpcRecipes
