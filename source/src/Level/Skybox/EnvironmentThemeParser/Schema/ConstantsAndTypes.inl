using detail::GdbView;
using detail::ReadBeF32;
using detail::ReadBeU32;
using detail::kHashParent;
constexpr uint32_t kHashLevelData = 0x123F8AFD;
constexpr uint32_t kHashEnvThemeGlobal = 0xAE17B958;
constexpr uint32_t kHashEnvironmentThemeDaySet = 0x0843AB41;
constexpr uint32_t kHashTheme = 0xB57E3290;
constexpr uint32_t kHashTimeOfDay = 0x9723C2C9;
constexpr uint32_t kHashWater = 0x1E6889DA;
constexpr uint32_t kHashSky = 0x2420BFA4;
constexpr uint32_t kHashLighting = 0x0B152C5D;
constexpr uint32_t kHashFogging = 0xDDF56C9A;
constexpr uint32_t kHashMainLightColour = 0x40A12D92;
constexpr uint32_t kHashSunIntensity = 0xC868C0DC;
constexpr uint32_t kHashSkyBetaRayleighMultiplier = 0x59837340;
constexpr uint32_t kHashSkyBetaMieMultiplier = 0xD7FC122C;
constexpr uint32_t kHashSkyColour = 0xD78A6E40;
constexpr uint32_t kHashSkyComplementaryColour = 0x5CBE1462;
constexpr uint32_t kHashSkyComplementaryColourBias = 0x2DA0C989;
constexpr uint32_t kHashSunsetColour = 0x897262B7;
constexpr uint32_t kHashSkyOverlayTexture = 0xF8C0DD95;
constexpr uint32_t kHashMoonTexture = 0x20D88F43;
constexpr uint32_t kHashMoonGlareTexture = 0xF2C7518C;
constexpr uint32_t kHashDiscTexture = 0x6B29E8A3;
constexpr uint32_t kHashClouds = 0x7439046F;
constexpr uint32_t kHashLayer1 = 0x6A570941;
constexpr uint32_t kHashLayer2 = 0x6A570942;
constexpr uint32_t kHashLayer3 = 0x6A570943;
constexpr uint32_t kHashLayer4 = 0x6A570944;
constexpr uint32_t kHashDensityMap = 0x13821B7F;
constexpr uint32_t kHashPositionX = 0x1E72B2E4;
constexpr uint32_t kHashPositionY = 0x1E72B2E5;
constexpr uint32_t kHashSizeX = 0x9C014CCE;
constexpr uint32_t kHashSizeY = 0x9C014CCF;
constexpr uint32_t kHashTextureScaleX = 0x0A8BA024;
constexpr uint32_t kHashTextureScaleY = 0x0A8BA025;
constexpr uint32_t kHashVelocityX = 0x5CE30740;
constexpr uint32_t kHashVelocityY = 0x5CE30741;
constexpr uint32_t kHashHeight = 0xF47DB020;
constexpr uint32_t kHashTransparency = 0x383FDB33;
constexpr uint32_t kHashNormalStrength = 0xB5B0AE93;
constexpr uint32_t kHashTranslucencyStrength = 0x114E67B1;
constexpr uint32_t kHashBrightness = 0xC452018C;
constexpr uint32_t kHashAmbientLight = 0x15DD1091;
constexpr uint32_t kHashWind = 0xBE284853;
constexpr uint32_t kHashGroundMist = 0x65AA790F;
constexpr uint32_t kHashStrength = 0x6CC36A2E;
constexpr uint32_t kHashRainDensity = 0x0043FE91;
constexpr uint32_t kHashRainSize = 0xF2494D2C;
constexpr uint32_t kHashSnowFallSpeed = 0x1C33AED8;
constexpr uint32_t kHashSnowSize = 0xC5487977;
constexpr uint32_t kHashWindStrengthMin = 0x467F5320;
constexpr uint32_t kHashWindStrengthMax = 0x3E7F46CE;
constexpr uint32_t kHashWindStrengthVariation = 0xEC5CF413;
constexpr uint32_t kHashWindXYRotationMin = 0x8724E1D8;
constexpr uint32_t kHashWindXYRotationMax = 0x8F24EE36;
constexpr uint32_t kHashWindElevationMin = 0xD6C55ADC;
constexpr uint32_t kHashWindElevationMax = 0xDEC56732;
constexpr uint32_t kHashWindChangeFrequency = 0xA1C12827;
constexpr uint32_t kHashWindChangeDuration = 0x5B45F873;
constexpr uint32_t kHashWindDirectionVariation = 0x5B71ED9D;
constexpr uint32_t kHashFoggingStart = 0x754D898A;

constexpr uint32_t kHashMoonIntensity = 0x8DA685FD;
constexpr uint32_t kHashMoonSize = 0x771E440D;
constexpr uint32_t kHashMoonGlareIntensity = 0x326BAB3E;
constexpr uint32_t kHashMoonGlareSize = 0x593E77C4;
constexpr uint32_t kHashMoonTransparency = 0xAF2DDBD4;
constexpr uint32_t kHashMoonAxisElevation = 0xF32010F6;
constexpr uint32_t kHashMoonAxisZOffset = 0x425F14D0;
constexpr uint32_t kHashMoonAxisXYRotationOffset = 0x695E86AB;
constexpr uint32_t kHashStarBrightness = 0xE52870D8;
constexpr uint32_t kHashDiscSize = 0x9C3FDEED;
constexpr uint32_t kHashDiscColour = 0x40EBF67E;
constexpr uint32_t kHashDiscIntensity = 0x7269BF5D;
constexpr uint32_t kHashSunBeamsWidth = 0x23777251;
constexpr uint32_t kHashSunBeamsHeight = 0x7FA6150A;
constexpr uint32_t kHashSunBeamsIntensity = 0x057D3A8A;
constexpr uint32_t kHashGlareIntensity = 0x7ADC8EE5;
constexpr uint32_t kHashGlareSize = 0x57AF2EF5;
constexpr uint32_t kHashSunBeamsTexture = 0xC5C32228;
constexpr uint32_t kHashGlareTexture = 0x495DCD2B;

constexpr uint32_t kHashSunRecord = 0x2620C36D;

constexpr uint32_t kHashSkyColourRed = 0x86B2D6AD;
constexpr uint32_t kHashSkyColourGreen = 0x0E4C7541;
constexpr uint32_t kHashSkyColourBlue = 0xFDCC27B4;
constexpr uint32_t kHashSkyColourFactor = 0x1273DB31;
constexpr uint32_t kHashSkyComplementaryColourRed = 0x507FFA3F;
constexpr uint32_t kHashSkyComplementaryColourGreen = 0xDA12836B;
constexpr uint32_t kHashSkyComplementaryColourBlue = 0x0593BF6A;
constexpr uint32_t kHashSkyComplementaryColourFactor = 0xD433126B;
constexpr uint32_t kHashSunsetColourRed = 0x896E8E84;
constexpr uint32_t kHashSunsetColourGreen = 0x8FC37AA4;
constexpr uint32_t kHashSunsetColourBlue = 0xCFED8B8F;
constexpr uint32_t kHashSunsetColourFactor = 0xB2447252;
constexpr uint32_t kHashCloseFogColourRed = 0x2229E682;
constexpr uint32_t kHashCloseFogColourGreen = 0x4D9D5A22;
constexpr uint32_t kHashCloseFogColourBlue = 0x876515F9;
constexpr uint32_t kHashCloseFogColourFactor = 0x010020C0;
constexpr uint32_t kHashDiscColourRed = 0x33E4BA23;
constexpr uint32_t kHashDiscColourGreen = 0x36D9C5CF;
constexpr uint32_t kHashDiscColourBlue = 0xBC29377E;
constexpr uint32_t kHashDiscColourFactor = 0x4BFBB0BF;
constexpr uint32_t kHashMainLightColourRed = 0x9F76036F;
constexpr uint32_t kHashMainLightColourGreen = 0xE3D88F9B;
constexpr uint32_t kHashMainLightColourBlue = 0x3E7D387A;
constexpr uint32_t kHashMainLightColourFactor = 0xE67DD6DB;
constexpr uint32_t kHashSunAxisElevation = 0x2682515B;
constexpr uint32_t kHashSunAxisZOffset = 0x2EF474B9;
constexpr uint32_t kHashSunAxisXYRotationOffset = 0x2E4D729C;
constexpr uint32_t kHashTimeFactor = 0x703AEFF3;
constexpr uint32_t kHashNearDistance = 0xDA3F7AAA;
constexpr uint32_t kHashNearDensity = 0x91F00AC5;
constexpr uint32_t kHashFarDistance = 0xFF154645;
constexpr uint32_t kHashFarDensity = 0xE1DF20B4;
constexpr uint32_t kHashCloseFogColour = 0x66353755;
constexpr uint32_t kHashCloseFogMaxDistance = 0xCC4BDF70;
constexpr uint32_t kHashRed = 0x3A232172;
constexpr uint32_t kHashGreen = 0x608C9792;
constexpr uint32_t kHashBlue = 0xB1911CC9;
constexpr uint32_t kHashFactor = 0xBF21DA70;
constexpr uint32_t kHashShallowWaterColourRed = 0xB2ECBF11;
constexpr uint32_t kHashShallowWaterColourGreen = 0x25F0E705;
constexpr uint32_t kHashShallowWaterColourBlue = 0x8BF17608;
constexpr uint32_t kHashDeepWaterColourRed = 0x1750476D;
constexpr uint32_t kHashDeepWaterColourGreen = 0x61671F01;
constexpr uint32_t kHashDeepWaterColourBlue = 0x632A3D74;
constexpr uint32_t kHashEdgeBlendBias = 0x79F85F10;
constexpr uint32_t kHashEdgeBlendMin = 0x5A234079;
constexpr uint32_t kHashEdgeBlendMax = 0x52233307;
constexpr uint32_t kHashMaxRefractionDistance = 0x671D3125;
constexpr uint32_t kHashFresnelBias = 0x73C59519;
constexpr uint32_t kHashReflectionStrength = 0xAF315449;
constexpr uint32_t kHashRefractionScale = 0x623C0662;
constexpr uint32_t kHashReflectionScale = 0x9BA6BDE0;
constexpr uint32_t kHashReflectionBias = 0x4838AA55;
constexpr uint32_t kHashNormalScale = 0xA027B7EE;
constexpr uint32_t kHashNull = 0x811C9DC5;

struct WaterThemeRecordRef {
    size_t db = 0;
    size_t record = 0;
    bool valid = false;
};

struct WaterThemeFieldRef {
    WaterThemeRecordRef owner;
    size_t slot = 0;
    uint8_t type = 0;
    uint32_t raw = 0;
    float f32 = 0.0f;
};

inline float Clamp01(float v)
{
    return std::clamp(v, 0.0f, 1.0f);
}

inline float EnvColourComponentToLinearInput(float v)
{

    return Clamp01(v * (1.0f / 255.0f));
}
