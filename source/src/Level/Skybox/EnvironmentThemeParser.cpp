#include "EnvironmentThemeParser.h"
#include "GdbReaderInternal.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <vector>

namespace Gdb {

namespace {

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
// FNV-1 of the XEX theme registration camelCase names (celestial elements).
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
// The XEX registers the sun disc/beams/streaks/glare params under a "Sun"
// group; in the GDB they live in a nested Sun sub-record of the Sky record.
constexpr uint32_t kHashSunRecord = 0x2620C36D;  // FNV-1 "Sun"
// Flat per-component colour fields (same convention as the water colours):
// <Base>Red/Green/Blue in 0..255 plus an optional <Base>Factor multiplier.
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

class EnvironmentThemeExtractor {
public:
    explicit EnvironmentThemeExtractor(
        const std::vector<const std::vector<uint8_t>*>& gdbs)
    {
        views_.reserve(gdbs.size());
        for (const auto* bytes : gdbs) {
            if (!bytes) continue;
            views_.emplace_back(*bytes);
        }
    }

    bool extract(WaterTheme& out_theme) const
    {
        out_theme = WaterTheme{};
        if (views_.empty()) return false;

        bool applied = false;

        WaterThemeRecordRef day_set = findDaySet(true);
        if (day_set.valid) {
            WaterThemeRecordRef day_theme;
            float day_time = -1.0f;
            if (selectThemeFromDaySet(day_set, day_theme, day_time)) {
                out_theme.source_time_of_day = day_time;
                applied |= applyThemeRecord(day_theme, out_theme);
            }
        }

        if (!applied) {
            for (const WaterThemeRecordRef& level :
                 lookupAllInDb(kHashLevelData, 0)) {
                applied |= applyThemeField(level, kHashEnvThemeGlobal,
                                           out_theme);
            }
        }

        if (!applied) {
            WaterThemeRecordRef owner =
                firstRecordWithFieldInDb(kHashEnvThemeGlobal, 0);
            if (owner.valid) {
                applied |= applyThemeField(owner, kHashEnvThemeGlobal,
                                           out_theme);
            }
        }

        if (!applied) {
            for (const WaterThemeRecordRef& global :
                 lookupAllInDb(kHashEnvThemeGlobal, 0)) {
                applied |= applyThemeRecord(global, out_theme);
            }
        }

        if (!applied) {
            day_set = findDaySet(false);
            WaterThemeRecordRef day_theme;
            float day_time = -1.0f;
            if (day_set.valid &&
                selectThemeFromDaySet(day_set, day_theme, day_time)) {
                out_theme.source_time_of_day = day_time;
                applied |= applyThemeRecord(day_theme, out_theme);
            }
        }

        if (!applied) {
            for (const WaterThemeRecordRef& global :
                 lookupAll(kHashEnvThemeGlobal)) {
                applied |= applyThemeRecord(global, out_theme);
            }
        }

        if (!applied) {
            applied = applyFirstWaterLikeRecord(out_theme);
        }

        out_theme.has_any = applied;
        return applied;
    }

    bool extractSky(SkyTheme& out_theme) const
    {
        out_theme = SkyTheme{};
        if (views_.empty()) return false;

        bool applied = false;

        WaterThemeRecordRef day_set = findDaySet(true);
        if (day_set.valid) {
            WaterThemeRecordRef day_theme;
            float day_time = -1.0f;
            if (selectThemeFromDaySet(day_set, day_theme, day_time)) {
                out_theme.source_time_of_day = day_time;
                applied |= applySkyThemeRecord(day_theme, out_theme);
            }
        }

        if (!applied) {
            for (const WaterThemeRecordRef& level :
                 lookupAllInDb(kHashLevelData, 0)) {
                applied |= applySkyThemeField(level, kHashEnvThemeGlobal,
                                              out_theme);
            }
        }

        if (!applied) {
            WaterThemeRecordRef owner =
                firstRecordWithFieldInDb(kHashEnvThemeGlobal, 0);
            if (owner.valid) {
                applied |= applySkyThemeField(owner, kHashEnvThemeGlobal,
                                              out_theme);
            }
        }

        if (!applied) {
            for (const WaterThemeRecordRef& global :
                 lookupAllInDb(kHashEnvThemeGlobal, 0)) {
                applied |= applySkyThemeRecord(global, out_theme);
            }
        }

        if (!applied) {
            day_set = findDaySet(false);
            WaterThemeRecordRef day_theme;
            float day_time = -1.0f;
            if (day_set.valid &&
                selectThemeFromDaySet(day_set, day_theme, day_time)) {
                out_theme.source_time_of_day = day_time;
                applied |= applySkyThemeRecord(day_theme, out_theme);
            }
        }

        if (!applied) {
            for (const WaterThemeRecordRef& global :
                 lookupAll(kHashEnvThemeGlobal)) {
                applied |= applySkyThemeRecord(global, out_theme);
            }
        }

        if (!applied) {
            applied = applyFirstSkyLikeRecord(out_theme);
        }

        out_theme.has_any = applied;
        return applied;
    }

    bool extractWeather(WeatherTheme& out_theme) const
    {
        out_theme = WeatherTheme{};
        if (views_.empty()) return false;

        bool applied = false;

        WaterThemeRecordRef day_set = findDaySet(true);
        if (day_set.valid) {
            WaterThemeRecordRef day_theme;
            float day_time = -1.0f;
            if (selectThemeFromDaySet(day_set, day_theme, day_time)) {
                out_theme.source_time_of_day = day_time;
                applied |= applyWeatherThemeRecord(day_theme, out_theme);
            }
        }

        if (!applied) {
            for (const WaterThemeRecordRef& level :
                 lookupAllInDb(kHashLevelData, 0)) {
                applied |= applyWeatherThemeField(level, kHashEnvThemeGlobal,
                                                  out_theme);
            }
        }

        if (!applied) {
            WaterThemeRecordRef owner =
                firstRecordWithFieldInDb(kHashEnvThemeGlobal, 0);
            if (owner.valid) {
                applied |= applyWeatherThemeField(owner, kHashEnvThemeGlobal,
                                                  out_theme);
            }
        }

        if (!applied) {
            for (const WaterThemeRecordRef& global :
                 lookupAllInDb(kHashEnvThemeGlobal, 0)) {
                applied |= applyWeatherThemeRecord(global, out_theme);
            }
        }

        if (!applied) {
            day_set = findDaySet(false);
            WaterThemeRecordRef day_theme;
            float day_time = -1.0f;
            if (day_set.valid &&
                selectThemeFromDaySet(day_set, day_theme, day_time)) {
                out_theme.source_time_of_day = day_time;
                applied |= applyWeatherThemeRecord(day_theme, out_theme);
            }
        }

        if (!applied) {
            for (const WaterThemeRecordRef& global :
                 lookupAll(kHashEnvThemeGlobal)) {
                applied |= applyWeatherThemeRecord(global, out_theme);
            }
        }

        out_theme.has_any = applied;
        return applied;
    }

    bool extractClouds(CloudTheme& out_theme) const
    {
        out_theme = CloudTheme{};
        if (views_.empty()) return false;

        bool applied = false;

        WaterThemeRecordRef day_set = findDaySet(true);
        if (day_set.valid) {
            WaterThemeRecordRef day_theme;
            float day_time = -1.0f;
            if (selectThemeFromDaySet(day_set, day_theme, day_time)) {
                out_theme.source_time_of_day = day_time;
                applied |= applyCloudThemeRecord(day_theme, out_theme);
            }
        }

        if (!applied) {
            for (const WaterThemeRecordRef& level :
                 lookupAllInDb(kHashLevelData, 0)) {
                applied |= applyCloudThemeField(level, kHashEnvThemeGlobal,
                                                out_theme);
            }
        }

        if (!applied) {
            WaterThemeRecordRef owner =
                firstRecordWithFieldInDb(kHashEnvThemeGlobal, 0);
            if (owner.valid) {
                applied |= applyCloudThemeField(owner, kHashEnvThemeGlobal,
                                                out_theme);
            }
        }

        if (!applied) {
            for (const WaterThemeRecordRef& global :
                 lookupAllInDb(kHashEnvThemeGlobal, 0)) {
                applied |= applyCloudThemeRecord(global, out_theme);
            }
        }

        if (!applied) {
            day_set = findDaySet(false);
            WaterThemeRecordRef day_theme;
            float day_time = -1.0f;
            if (day_set.valid &&
                selectThemeFromDaySet(day_set, day_theme, day_time)) {
                out_theme.source_time_of_day = day_time;
                applied |= applyCloudThemeRecord(day_theme, out_theme);
            }
        }

        if (!applied) {
            for (const WaterThemeRecordRef& global :
                 lookupAll(kHashEnvThemeGlobal)) {
                applied |= applyCloudThemeRecord(global, out_theme);
            }
        }

        // A standalone environmentthemes.gdb has no LevelData owner.  Its
        // concrete Clouds record is still unambiguous: it is the record that
        // owns the four fixed Layer1..Layer4 references.
        if (!applied) {
            WaterThemeRecordRef clouds = firstCloudContainer();
            if (clouds.valid) {
                applied |= applyCloudThemeRecord(clouds, out_theme);
            }
        }

        finaliseCloudTheme(out_theme);
        out_theme.has_any = applied && out_theme.layer_count > 0;
        return out_theme.has_any;
    }

    bool extractEnvironmentTimeline(EnvironmentThemeTimeline& out_timeline)
        const
    {
        out_timeline = EnvironmentThemeTimeline{};
        if (views_.empty()) return false;

        WaterThemeRecordRef day_set = findDaySet(true);
        if (!day_set.valid) {
            day_set = findDaySet(false);
        }
#ifdef FABLE_THEME_PARSER_TRACE
        std::fprintf(stderr, "[timeline] day_set valid=%d db=%zu\n",
                     day_set.valid ? 1 : 0, day_set.db);
#endif
        if (!day_set.valid) return false;

        const GdbView& v = view(day_set);
        size_t sch = 0;
        uint32_t n = 0;
        if (!v.schema(day_set.record, sch, n)) return false;
#ifdef FABLE_THEME_PARSER_TRACE
        std::fprintf(stderr, "[timeline] fields=%u\n", n);
#endif

        // The field-descriptor table lives in the schema area (between
        // schema_base and hash_base), NOT in the record body; bounding it
        // against body_end rejected every day set.
        const size_t descs = sch + 4 + size_t(n) * 4;
        if (descs + size_t(n) * 4 > v.hash_base) return false;

        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t desc =
                ReadBeU32(v.bytes.data() + descs + size_t(i) * 4);
            const uint8_t type = uint8_t(desc >> 24);
            if (type != 4 && type != 6 && type != 7) continue;

            const size_t slot = day_set.record + 4 + size_t(i) * 4;
            if (slot + 4 > v.body_end) continue;

            WaterThemeFieldRef item_field;
            item_field.owner = day_set;
            item_field.slot = slot;
            item_field.type = type;
            item_field.raw = ReadBeU32(v.bytes.data() + slot);
            item_field.f32 = ReadBeF32(v.bytes.data() + slot);

            WaterThemeRecordRef entry = fieldToRecord(item_field);
            if (!entry.valid) continue;

            WaterThemeRecordRef theme =
                resolveRecordField(entry, kHashTheme);
            if (!theme.valid) continue;

            float time = 0.5f;
            float read_time = 0.0f;
            if (readFloat(entry, kHashTimeOfDay, read_time)) {
                time = normalizeTimeOfDay(read_time);
            }

            EnvironmentThemeKeyframe key;
            key.time_of_day = time;

            const bool water_applied = applyThemeRecord(theme, key.water);
            key.water.has_any = water_applied;
            key.water.source_time_of_day = time;

            const bool sky_applied = applySkyThemeRecord(theme, key.sky);
            key.sky.has_any = sky_applied;
            key.sky.source_time_of_day = time;

            const bool cloud_applied =
                applyCloudThemeRecord(theme, key.clouds);
            finaliseCloudTheme(key.clouds);
            key.clouds.has_any =
                cloud_applied && key.clouds.layer_count > 0;
            key.clouds.source_time_of_day = time;

            const bool weather_applied =
                applyWeatherThemeRecord(theme, key.weather);
            key.weather.has_any = weather_applied;
            key.weather.source_time_of_day = time;

            if (key.water.has_any || key.sky.has_any ||
                key.clouds.has_any || key.weather.has_any) {
                out_timeline.keyframes.push_back(key);
            }
        }

        std::sort(out_timeline.keyframes.begin(),
                  out_timeline.keyframes.end(),
                  [](const EnvironmentThemeKeyframe& a,
                     const EnvironmentThemeKeyframe& b) {
                      return a.time_of_day < b.time_of_day;
                  });
        out_timeline.keyframes.erase(
            std::unique(out_timeline.keyframes.begin(),
                        out_timeline.keyframes.end(),
                        [](const EnvironmentThemeKeyframe& a,
                           const EnvironmentThemeKeyframe& b) {
                            return std::fabs(a.time_of_day -
                                             b.time_of_day) < 0.0005f;
                        }),
            out_timeline.keyframes.end());

        out_timeline.has_any = out_timeline.keyframes.size() >= 2;
        return out_timeline.has_any;
    }

private:
    std::vector<GdbView> views_;

    static float normalizeTimeOfDay(float time)
    {
        if (!std::isfinite(time)) return 0.5f;
        if (time > 1.0f && time <= 24.0f) {
            time *= (1.0f / 24.0f);
        }
        time -= std::floor(time);
        if (time < 0.0f) time += 1.0f;
        return time;
    }

    const GdbView& view(const WaterThemeRecordRef& ref) const
    {
        return views_[ref.db];
    }

    std::vector<WaterThemeRecordRef> lookupAll(uint32_t hash) const
    {
        std::vector<WaterThemeRecordRef> out;
        if (hash == 0 || hash == kHashNull) return out;
        for (size_t db = 0; db < views_.size(); ++db) {
            const GdbView& v = views_[db];
            if (!v.ok) continue;
            size_t record = 0;
            if (v.lookup(hash, record)) {
                out.push_back(WaterThemeRecordRef{db, record, true});
            }
        }
        return out;
    }

    std::vector<WaterThemeRecordRef> lookupAllInDb(uint32_t hash,
                                                   size_t db) const
    {
        std::vector<WaterThemeRecordRef> out;
        if (hash == 0 || hash == kHashNull || db >= views_.size()) {
            return out;
        }
        const GdbView& v = views_[db];
        if (!v.ok) return out;
        size_t record = 0;
        if (v.lookup(hash, record)) {
            out.push_back(WaterThemeRecordRef{db, record, true});
        }
        return out;
    }

    WaterThemeRecordRef lookupFirst(uint32_t hash) const
    {
        const std::vector<WaterThemeRecordRef> all = lookupAll(hash);
        if (all.empty()) return {};
        return all.front();
    }

    WaterThemeRecordRef firstRecordWithField(uint32_t field_hash) const
    {
        for (size_t db = 0; db < views_.size(); ++db) {
            const GdbView& v = views_[db];
            if (!v.ok) continue;
            for (size_t record : v.record_data_offsets) {
                size_t slot = 0;
                if (v.findLocal(record, field_hash, 0xFF,
                                slot, nullptr)) {
                    return WaterThemeRecordRef{db, record, true};
                }
            }
        }
        return {};
    }

    WaterThemeRecordRef firstRecordWithFieldInDb(uint32_t field_hash,
                                                 size_t db) const
    {
        if (db >= views_.size()) return {};
        const GdbView& v = views_[db];
        if (!v.ok) return {};
        for (size_t record : v.record_data_offsets) {
            size_t slot = 0;
            if (v.findLocal(record, field_hash, 0xFF, slot, nullptr)) {
                return WaterThemeRecordRef{db, record, true};
            }
        }
        return {};
    }

    WaterThemeRecordRef firstCloudContainer() const
    {
        constexpr uint32_t kLayerFields[4] = {
            kHashLayer1, kHashLayer2, kHashLayer3, kHashLayer4
        };
        for (size_t db = 0; db < views_.size(); ++db) {
            const GdbView& v = views_[db];
            if (!v.ok) continue;
            for (size_t record : v.record_data_offsets) {
                bool has_all_layers = true;
                for (uint32_t hash : kLayerFields) {
                    size_t slot = 0;
                    if (!v.findLocal(record, hash, 0xFF, slot, nullptr)) {
                        has_all_layers = false;
                        break;
                    }
                }
                if (has_all_layers) {
                    return WaterThemeRecordRef{db, record, true};
                }
            }
        }
        return {};
    }

    WaterThemeRecordRef findDaySet(bool level_only) const
    {
        const size_t db_count = level_only
            ? std::min<size_t>(views_.size(), 1)
            : views_.size();
        for (size_t db = 0; db < db_count; ++db) {
            for (const WaterThemeRecordRef& level :
                 lookupAllInDb(kHashLevelData, db)) {
                WaterThemeRecordRef day_set =
                    resolveRecordField(level, kHashEnvironmentThemeDaySet);
                if (day_set.valid) return day_set;
            }
        }

        // Levels carry more than one record with an EnvironmentThemeDaySet
        // field and some are null references (e.g. a template record with
        // value 0); try every owner and keep the first that resolves.
        for (size_t db = 0; db < db_count; ++db) {
            if (db >= views_.size()) continue;
            const GdbView& v = views_[db];
            if (!v.ok) continue;
            for (size_t record : v.record_data_offsets) {
                size_t slot = 0;
                if (!v.findLocal(record, kHashEnvironmentThemeDaySet, 0xFF,
                                 slot, nullptr)) {
                    continue;
                }
                WaterThemeRecordRef owner{db, record, true};
                WaterThemeRecordRef day_set = resolveRecordField(
                    owner, kHashEnvironmentThemeDaySet);
                if (day_set.valid) return day_set;
            }
        }

        return {};
    }

    bool findLocalField(WaterThemeRecordRef record,
                        uint32_t field_hash,
                        uint8_t expected_type,
                        WaterThemeFieldRef& out) const
    {
        if (!record.valid || record.db >= views_.size()) return false;
        const GdbView& v = view(record);
        size_t slot = 0;
        uint8_t type = 0;
        if (!v.findLocal(record.record, field_hash, expected_type,
                         slot, &type)) {
            return false;
        }
        out.owner = record;
        out.slot = slot;
        out.type = type;
        out.raw = ReadBeU32(v.bytes.data() + slot);
        out.f32 = ReadBeF32(v.bytes.data() + slot);
        return true;
    }

    bool findField(WaterThemeRecordRef record,
                   uint32_t field_hash,
                   uint8_t expected_type,
                   WaterThemeFieldRef& out) const
    {
        if (!record.valid || record.db >= views_.size()) return false;

        WaterThemeRecordRef cur = record;
        std::unordered_set<uint64_t> seen;
        for (int depth = 0; depth < 64; ++depth) {
            const uint64_t key =
                (uint64_t(cur.db) << 48) ^ uint64_t(cur.record);
            if (!seen.insert(key).second) return false;

            if (findLocalField(cur, field_hash, expected_type, out)) {
                return true;
            }

            WaterThemeFieldRef parent_field;
            if (!findLocalField(cur, kHashParent, 0xFF, parent_field)) {
                return false;
            }
            cur = fieldToRecord(parent_field);
            if (!cur.valid) return false;
        }
        return false;
    }

    WaterThemeRecordRef fieldToRecord(const WaterThemeFieldRef& field) const
    {
        if (field.raw == 0 || field.raw == kHashNull) return {};
        if (field.type != 4 && field.type != 6 && field.type != 7) return {};
        return lookupFirst(field.raw);
    }

    WaterThemeRecordRef resolveRecordField(WaterThemeRecordRef record,
                                           uint32_t field_hash) const
    {
        WaterThemeFieldRef field;
        if (!findField(record, field_hash, 0xFF, field)) return {};
        return fieldToRecord(field);
    }

    bool readFloat(WaterThemeRecordRef record,
                   uint32_t field_hash,
                   float& out) const
    {
        WaterThemeFieldRef field;
        if (!findField(record, field_hash, 3, field)) return false;
        if (!std::isfinite(field.f32)) return false;
        out = field.f32;
        return true;
    }

    bool readColour(WaterThemeRecordRef record,
                    uint32_t red_hash,
                    uint32_t green_hash,
                    uint32_t blue_hash,
                    float (&out)[3]) const
    {
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        if (!readFloat(record, red_hash, r) ||
            !readFloat(record, green_hash, g) ||
            !readFloat(record, blue_hash, b)) {
            return false;
        }
        out[0] = EnvColourComponentToLinearInput(r);
        out[1] = EnvColourComponentToLinearInput(g);
        out[2] = EnvColourComponentToLinearInput(b);
        return true;
    }

    // Flat per-component colour on the record itself (0..255 components,
    // optional multiplier field).
    bool readFlatColour(WaterThemeRecordRef record,
                        uint32_t red_hash,
                        uint32_t green_hash,
                        uint32_t blue_hash,
                        uint32_t factor_hash,
                        float (&out)[3]) const
    {
        if (!readColour(record, red_hash, green_hash, blue_hash, out)) {
            return false;
        }
        float factor = 1.0f;
        if (factor_hash != 0 &&
            readFloat(record, factor_hash, factor) &&
            std::isfinite(factor) && factor > 0.0f) {
            out[0] *= factor;
            out[1] *= factor;
            out[2] *= factor;
        }
        return true;
    }

    bool readColourRecordField(WaterThemeRecordRef record,
                               uint32_t field_hash,
                               float (&out)[3]) const
    {
        WaterThemeRecordRef colour = resolveRecordField(record, field_hash);
        if (!colour.valid) return false;
        if (!readColour(colour, kHashRed, kHashGreen, kHashBlue, out)) {
            return false;
        }
        float factor = 1.0f;
        if (readFloat(colour, kHashFactor, factor) &&
            std::isfinite(factor) && factor > 0.0f) {
            out[0] *= factor;
            out[1] *= factor;
            out[2] *= factor;
        }
        return true;
    }

    bool applyWaterRecord(WaterThemeRecordRef water,
                          WaterTheme& theme) const
    {
        if (!water.valid) return false;

        bool any = false;
        float colour[3] = {};
        if (readColour(water,
                       kHashShallowWaterColourRed,
                       kHashShallowWaterColourGreen,
                       kHashShallowWaterColourBlue,
                       colour)) {
            std::copy(std::begin(colour), std::end(colour),
                      std::begin(theme.shallow_colour));
            theme.has_shallow_colour = true;
            any = true;
        }
        if (readColour(water,
                       kHashDeepWaterColourRed,
                       kHashDeepWaterColourGreen,
                       kHashDeepWaterColourBlue,
                       colour)) {
            std::copy(std::begin(colour), std::end(colour),
                      std::begin(theme.deep_colour));
            theme.has_deep_colour = true;
            any = true;
        }

        auto read_param = [&](uint32_t hash, bool& flag, float& dst) {
            float v = 0.0f;
            if (readFloat(water, hash, v)) {
                flag = true;
                dst = v;
                any = true;
            }
        };

        read_param(kHashEdgeBlendMin, theme.has_edge_blend_min,
                   theme.edge_blend_min);
        read_param(kHashEdgeBlendMax, theme.has_edge_blend_max,
                   theme.edge_blend_max);
        read_param(kHashEdgeBlendBias, theme.has_edge_blend_bias,
                   theme.edge_blend_bias);
        read_param(kHashMaxRefractionDistance,
                   theme.has_max_refraction_distance,
                   theme.max_refraction_distance);
        read_param(kHashFresnelBias, theme.has_fresnel_bias,
                   theme.fresnel_bias);
        read_param(kHashReflectionStrength,
                   theme.has_reflection_strength,
                   theme.reflection_strength);
        read_param(kHashRefractionScale, theme.has_refraction_scale,
                   theme.refraction_scale);
        read_param(kHashReflectionScale, theme.has_reflection_scale,
                   theme.reflection_scale);
        read_param(kHashReflectionBias, theme.has_reflection_bias,
                   theme.reflection_bias);
        read_param(kHashNormalScale, theme.has_normal_scale,
                   theme.normal_scale);

        return any;
    }

    bool applyThemeRecord(WaterThemeRecordRef theme_record,
                          WaterTheme& theme) const
    {
        if (!theme_record.valid) return false;
        WaterThemeRecordRef water =
            resolveRecordField(theme_record, kHashWater);
        if (water.valid && applyWaterRecord(water, theme)) {
            return true;
        }
        return applyWaterRecord(theme_record, theme);
    }

    bool applyThemeField(WaterThemeRecordRef owner,
                         uint32_t field_hash,
                         WaterTheme& theme) const
    {
        WaterThemeRecordRef target = resolveRecordField(owner, field_hash);
        return target.valid && applyThemeRecord(target, theme);
    }

    bool applySkyRecord(WaterThemeRecordRef sky,
                        SkyTheme& theme) const
    {
        if (!sky.valid) return false;

        bool any = false;
        float colour[3] = {};
        if (readColourRecordField(sky, kHashSkyColour, colour) ||
            readFlatColour(sky, kHashSkyColourRed, kHashSkyColourGreen,
                           kHashSkyColourBlue, kHashSkyColourFactor,
                           colour)) {
            std::copy(std::begin(colour), std::end(colour),
                      std::begin(theme.sky_colour));
            theme.has_sky_colour = true;
            any = true;
        }
        if (readColourRecordField(sky, kHashSkyComplementaryColour,
                                  colour) ||
            readFlatColour(sky, kHashSkyComplementaryColourRed,
                           kHashSkyComplementaryColourGreen,
                           kHashSkyComplementaryColourBlue,
                           kHashSkyComplementaryColourFactor, colour)) {
            std::copy(std::begin(colour), std::end(colour),
                      std::begin(theme.complementary_colour));
            theme.has_complementary_colour = true;
            any = true;
        }
        if (readColourRecordField(sky, kHashSunsetColour, colour) ||
            readFlatColour(sky, kHashSunsetColourRed,
                           kHashSunsetColourGreen, kHashSunsetColourBlue,
                           kHashSunsetColourFactor, colour)) {
            std::copy(std::begin(colour), std::end(colour),
                      std::begin(theme.sunset_colour));
            theme.has_sunset_colour = true;
            any = true;
        }

        auto read_param = [&](uint32_t hash, bool& flag, float& dst) {
            float v = 0.0f;
            if (readFloat(sky, hash, v)) {
                flag = true;
                dst = v;
                any = true;
            }
        };

        read_param(kHashSunIntensity, theme.has_sun_intensity,
                   theme.sun_intensity);
        read_param(kHashSkyBetaRayleighMultiplier, theme.has_rayleigh,
                   theme.rayleigh);
        read_param(kHashSkyBetaMieMultiplier, theme.has_mie,
                   theme.mie);
        read_param(kHashSkyComplementaryColourBias,
                   theme.has_complementary_bias,
                   theme.complementary_bias);
        read_param(kHashSunAxisElevation, theme.has_sun_axis,
                   theme.sun_axis_elevation);
        read_param(kHashSunAxisZOffset, theme.has_sun_axis,
                   theme.sun_axis_z_offset);
        read_param(kHashSunAxisXYRotationOffset, theme.has_sun_axis,
                   theme.sun_axis_xy_rotation);
        {
            float tf = 1.0f;
            if (readFloat(sky, kHashTimeFactor, tf) &&
                std::isfinite(tf) && tf > 0.0f) {
                theme.main_light_time_factor = tf;
            }
        }

        auto read_texture = [&](uint32_t hash, bool& flag, uint32_t& dst) {
            WaterThemeFieldRef field;
            if (findField(sky, hash, 0xFF, field) &&
                field.raw != 0 && field.raw != kHashNull) {
                flag = true;
                dst = field.raw;
                any = true;
            }
        };
        read_texture(kHashSkyOverlayTexture,
                     theme.has_sky_overlay_texture,
                     theme.sky_overlay_texture_hash);
        read_texture(kHashMoonTexture,
                     theme.has_moon_texture,
                     theme.moon_texture_hash);
        read_texture(kHashMoonGlareTexture,
                     theme.has_moon_glare_texture,
                     theme.moon_glare_texture_hash);
        read_texture(kHashDiscTexture,
                     theme.has_sun_disc_texture,
                     theme.sun_disc_texture_hash);
        read_texture(kHashSunBeamsTexture,
                     theme.has_sun_beams_texture,
                     theme.sun_beams_texture_hash);
        read_texture(kHashGlareTexture,
                     theme.has_sun_glare_texture,
                     theme.sun_glare_texture_hash);

        read_param(kHashMoonIntensity, theme.has_moon_params,
                   theme.moon_intensity);
        read_param(kHashMoonSize, theme.has_moon_params,
                   theme.moon_size);
        read_param(kHashMoonGlareIntensity, theme.has_moon_params,
                   theme.moon_glare_intensity);
        read_param(kHashMoonGlareSize, theme.has_moon_params,
                   theme.moon_glare_size);
        read_param(kHashMoonTransparency, theme.has_moon_params,
                   theme.moon_transparency);
        read_param(kHashMoonAxisElevation, theme.has_moon_axis,
                   theme.moon_axis_elevation);
        read_param(kHashMoonAxisZOffset, theme.has_moon_axis,
                   theme.moon_axis_z_offset);
        read_param(kHashMoonAxisXYRotationOffset, theme.has_moon_axis,
                   theme.moon_axis_xy_rotation);
        read_param(kHashStarBrightness, theme.has_star_brightness,
                   theme.star_brightness);

        return any;
    }

    // Sun-group fields (disc/beams/glare/streaks) live in a Sun sub-record
    // that is a sibling of Sky/Fogging/Lighting on the theme record.
    bool applySunRecord(WaterThemeRecordRef sun, SkyTheme& theme) const
    {
        if (!sun.valid) return false;

        bool any = false;
        auto read_param = [&](uint32_t hash, bool& flag, float& dst) {
            float v = 0.0f;
            if (readFloat(sun, hash, v)) {
                flag = true;
                dst = v;
                any = true;
            }
        };
        read_param(kHashDiscSize, theme.has_sun_disc_params,
                   theme.sun_disc_size);
        read_param(kHashDiscIntensity, theme.has_sun_disc_params,
                   theme.sun_disc_intensity);
        float colour[3] = {};
        if (readColourRecordField(sun, kHashDiscColour, colour) ||
            readFlatColour(sun, kHashDiscColourRed, kHashDiscColourGreen,
                           kHashDiscColourBlue, kHashDiscColourFactor,
                           colour)) {
            std::copy(std::begin(colour), std::end(colour),
                      std::begin(theme.sun_disc_colour));
            theme.has_sun_disc_colour = true;
            any = true;
        }
        read_param(kHashSunBeamsWidth, theme.has_sun_beams,
                   theme.sun_beams_width);
        read_param(kHashSunBeamsHeight, theme.has_sun_beams,
                   theme.sun_beams_height);
        read_param(kHashSunBeamsIntensity, theme.has_sun_beams,
                   theme.sun_beams_intensity);
        read_param(kHashGlareIntensity, theme.has_sun_glare,
                   theme.sun_glare_intensity);
        read_param(kHashGlareSize, theme.has_sun_glare,
                   theme.sun_glare_size);
        auto read_texture = [&](uint32_t hash, bool& flag, uint32_t& dst) {
            WaterThemeFieldRef field;
            if (findField(sun, hash, 0xFF, field) &&
                field.raw != 0 && field.raw != kHashNull) {
                flag = true;
                dst = field.raw;
                any = true;
            }
        };
        read_texture(kHashDiscTexture,
                     theme.has_sun_disc_texture,
                     theme.sun_disc_texture_hash);
        read_texture(kHashSunBeamsTexture,
                     theme.has_sun_beams_texture,
                     theme.sun_beams_texture_hash);
        read_texture(kHashGlareTexture,
                     theme.has_sun_glare_texture,
                     theme.sun_glare_texture_hash);
        return any;
    }

    bool applyFogRecord(WaterThemeRecordRef fog,
                        SkyTheme& theme) const
    {
        if (!fog.valid) return false;

        bool any = false;
        float colour[3] = {};
        if (readColourRecordField(fog, kHashCloseFogColour, colour) ||
            readFlatColour(fog, kHashCloseFogColourRed,
                           kHashCloseFogColourGreen,
                           kHashCloseFogColourBlue,
                           kHashCloseFogColourFactor, colour)) {
            std::copy(std::begin(colour), std::end(colour),
                      std::begin(theme.fog_colour));
            theme.has_fog_colour = true;
            any = true;
        }

        float v = 0.0f;
        if (readFloat(fog, kHashNearDistance, v)) {
            theme.near_distance = v;
            theme.has_near_fog = true;
            any = true;
        }
        if (readFloat(fog, kHashNearDensity, v)) {
            theme.near_density = v;
            theme.has_near_fog = true;
            any = true;
        }
        if (readFloat(fog, kHashFarDistance, v)) {
            theme.far_distance = v;
            theme.has_far_fog = true;
            any = true;
        }
        if (readFloat(fog, kHashFarDensity, v)) {
            theme.far_density = v;
            theme.has_far_fog = true;
            any = true;
        }
        if (readFloat(fog, kHashCloseFogMaxDistance, v)) {
            theme.close_fog_max_distance = v;
            any = true;
        }
        if (readFloat(fog, kHashFoggingStart, v)) {
            theme.fogging_start = v;
            theme.has_fogging_start = true;
            any = true;
        }

        return any;
    }

    bool applySkyThemeRecord(WaterThemeRecordRef theme_record,
                             SkyTheme& theme) const
    {
        if (!theme_record.valid) return false;

        bool any = false;
        WaterThemeRecordRef sky =
            resolveRecordField(theme_record, kHashSky);
        if (sky.valid) {
            any |= applySkyRecord(sky, theme);
        } else {
            any |= applySkyRecord(theme_record, theme);
        }

        WaterThemeRecordRef fog =
            resolveRecordField(theme_record, kHashFogging);
        if (fog.valid) {
            any |= applyFogRecord(fog, theme);
        } else {
            any |= applyFogRecord(theme_record, theme);
        }

        WaterThemeRecordRef sun =
            resolveRecordField(theme_record, kHashSunRecord);
        if (sun.valid) {
            any |= applySunRecord(sun, theme);
        } else if (sky.valid) {
            any |= applySunRecord(sky, theme);
        }

        WaterThemeRecordRef lighting =
            resolveRecordField(theme_record, kHashLighting);
        if (lighting.valid) {
            float colour[3] = {};
            if (readColourRecordField(lighting, kHashMainLightColour,
                                      colour) ||
                readFlatColour(lighting, kHashMainLightColourRed,
                               kHashMainLightColourGreen,
                               kHashMainLightColourBlue,
                               kHashMainLightColourFactor, colour)) {
                std::copy(std::begin(colour), std::end(colour),
                          std::begin(theme.main_light_colour));
                theme.has_main_light_colour = true;
                any = true;
            }
        }

        return any;
    }

    bool applySkyThemeField(WaterThemeRecordRef owner,
                            uint32_t field_hash,
                            SkyTheme& theme) const
    {
        WaterThemeRecordRef target = resolveRecordField(owner, field_hash);
        return target.valid && applySkyThemeRecord(target, theme);
    }

    bool applyWeatherSkyRecord(WaterThemeRecordRef sky,
                               WeatherTheme& theme) const
    {
        if (!sky.valid) return false;

        bool any = false;
        auto read_param = [&](uint32_t hash, bool& flag, float& dst) {
            float v = 0.0f;
            if (readFloat(sky, hash, v)) {
                flag = true;
                dst = v;
                any = true;
            }
        };
        read_param(kHashRainDensity, theme.has_rain, theme.rain_density);
        read_param(kHashRainSize, theme.has_rain, theme.rain_size);
        read_param(kHashSnowFallSpeed, theme.has_snow,
                   theme.snow_fall_speed);
        read_param(kHashSnowSize, theme.has_snow, theme.snow_size);
        return any;
    }

    bool applyWeatherWindRecord(WaterThemeRecordRef wind,
                                WeatherTheme& theme) const
    {
        if (!wind.valid) return false;

        bool any = false;
        auto read_param = [&](uint32_t hash, float& dst) {
            float v = 0.0f;
            if (readFloat(wind, hash, v)) {
                theme.has_wind = true;
                dst = v;
                any = true;
            }
        };
        read_param(kHashWindStrengthMin, theme.wind_strength_min);
        read_param(kHashWindStrengthMax, theme.wind_strength_max);
        read_param(kHashWindStrengthVariation,
                   theme.wind_strength_variation);
        read_param(kHashWindXYRotationMin, theme.wind_xy_rotation_min);
        read_param(kHashWindXYRotationMax, theme.wind_xy_rotation_max);
        read_param(kHashWindElevationMin, theme.wind_elevation_min);
        read_param(kHashWindElevationMax, theme.wind_elevation_max);
        read_param(kHashWindChangeFrequency, theme.wind_change_frequency);
        read_param(kHashWindChangeDuration, theme.wind_change_duration);
        read_param(kHashWindDirectionVariation,
                   theme.wind_direction_variation);
        return any;
    }

    bool applyWeatherMistRecord(WaterThemeRecordRef mist,
                                WeatherTheme& theme) const
    {
        if (!mist.valid) return false;

        float v = 0.0f;
        if (readFloat(mist, kHashStrength, v)) {
            theme.has_ground_mist = true;
            theme.ground_mist_strength = v;
            return true;
        }
        return false;
    }

    bool applyWeatherThemeRecord(WaterThemeRecordRef theme_record,
                                 WeatherTheme& theme) const
    {
        if (!theme_record.valid) return false;

        bool any = false;
        WaterThemeRecordRef sky =
            resolveRecordField(theme_record, kHashSky);
        if (sky.valid) {
            any |= applyWeatherSkyRecord(sky, theme);
        } else {
            any |= applyWeatherSkyRecord(theme_record, theme);
        }

        WaterThemeRecordRef wind =
            resolveRecordField(theme_record, kHashWind);
        if (wind.valid) {
            any |= applyWeatherWindRecord(wind, theme);
        } else {
            any |= applyWeatherWindRecord(theme_record, theme);
        }

        WaterThemeRecordRef mist =
            resolveRecordField(theme_record, kHashGroundMist);
        if (mist.valid) {
            any |= applyWeatherMistRecord(mist, theme);
        }
        return any;
    }

    bool applyWeatherThemeField(WaterThemeRecordRef owner,
                                uint32_t field_hash,
                                WeatherTheme& theme) const
    {
        WaterThemeRecordRef target = resolveRecordField(owner, field_hash);
        return target.valid && applyWeatherThemeRecord(target, theme);
    }

    int readCloudLayerRecord(WaterThemeRecordRef layer,
                             CloudLayerTheme& out) const
    {
        if (!layer.valid) return 0;

        int fields = 0;
        auto read_param = [&](uint32_t hash, bool& flag, float& dst) {
            float v = 0.0f;
            if (readFloat(layer, hash, v)) {
                flag = true;
                dst = v;
                ++fields;
            }
        };

        WaterThemeFieldRef density;
        if (findField(layer, kHashDensityMap, 0xFF, density) &&
            density.raw != 0 && density.raw != kHashNull) {
            out.has_density_map = true;
            out.density_map_hash = density.raw;
            ++fields;
        }

        read_param(kHashPositionX, out.has_position, out.position_x);
        read_param(kHashPositionY, out.has_position, out.position_y);
        read_param(kHashSizeX, out.has_size, out.size_x);
        read_param(kHashSizeY, out.has_size, out.size_y);
        read_param(kHashTextureScaleX, out.has_texture_scale,
                   out.texture_scale_x);
        read_param(kHashTextureScaleY, out.has_texture_scale,
                   out.texture_scale_y);
        read_param(kHashVelocityX, out.has_velocity, out.velocity_x);
        read_param(kHashVelocityY, out.has_velocity, out.velocity_y);
        read_param(kHashHeight, out.has_height, out.height);
        read_param(kHashTransparency, out.has_transparency,
                   out.transparency);
        read_param(kHashBrightness, out.has_brightness, out.brightness);
        read_param(kHashAmbientLight, out.has_ambient,
                   out.ambient_light);
        read_param(kHashNormalStrength, out.has_normal_strength,
                   out.normal_strength);
        read_param(kHashTranslucencyStrength,
                   out.has_translucency_strength,
                   out.translucency_strength);

        if (fields > 0) {
            out.enabled = true;
        }
        return fields;
    }

    bool applyCloudLayerField(WaterThemeRecordRef theme_record,
                              uint32_t field_hash,
                              CloudLayerTheme& layer) const
    {
        WaterThemeRecordRef target = resolveRecordField(theme_record,
                                                        field_hash);
        return target.valid && readCloudLayerRecord(target, layer) > 0;
    }

    bool applyCloudThemeRecord(WaterThemeRecordRef theme_record,
                               CloudTheme& theme) const
    {
        if (!theme_record.valid) return false;

        WaterThemeRecordRef clouds =
            resolveRecordField(theme_record, kHashClouds);
        if (!clouds.valid) {
            clouds = theme_record;
        }

        bool any = false;
        constexpr uint32_t kLayerFields[4] = {
            kHashLayer1, kHashLayer2, kHashLayer3, kHashLayer4
        };
        for (int i = 0; i < 4; ++i) {
            CloudLayerTheme layer = theme.layers[i];
            if (applyCloudLayerField(clouds, kLayerFields[i], layer)) {
                theme.layers[i] = layer;
                any = true;
            }
        }

        if (!any) {
            CloudLayerTheme layer = theme.layers[0];
            const int field_count = readCloudLayerRecord(clouds, layer);
            if (field_count >= 2 || layer.has_density_map) {
                theme.layers[0] = layer;
                any = true;
            }
        }

        return any;
    }

    bool applyCloudThemeField(WaterThemeRecordRef owner,
                              uint32_t field_hash,
                              CloudTheme& theme) const
    {
        WaterThemeRecordRef target = resolveRecordField(owner, field_hash);
        return target.valid && applyCloudThemeRecord(target, theme);
    }

    bool selectThemeFromDaySet(WaterThemeRecordRef day_set,
                               WaterThemeRecordRef& out_theme,
                               float& out_time) const
    {
        if (!day_set.valid) return false;
        const GdbView& v = view(day_set);
        size_t sch = 0;
        uint32_t n = 0;
        if (!v.schema(day_set.record, sch, n)) return false;
        const size_t descs = sch + 4 + size_t(n) * 4;

        bool found = false;
        float best_dist = std::numeric_limits<float>::max();
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t desc =
                ReadBeU32(v.bytes.data() + descs + size_t(i) * 4);
            const uint8_t type = uint8_t(desc >> 24);
            if (type != 4 && type != 6 && type != 7) continue;

            const size_t slot = day_set.record + 4 + size_t(i) * 4;
            if (slot + 4 > v.body_end) continue;

            WaterThemeFieldRef item_field;
            item_field.owner = day_set;
            item_field.slot = slot;
            item_field.type = type;
            item_field.raw = ReadBeU32(v.bytes.data() + slot);
            item_field.f32 = ReadBeF32(v.bytes.data() + slot);

            WaterThemeRecordRef entry = fieldToRecord(item_field);
            if (!entry.valid) continue;

            WaterThemeRecordRef theme =
                resolveRecordField(entry, kHashTheme);
            if (!theme.valid) continue;

            float time = 0.5f;
            float read_time = 0.0f;
            if (readFloat(entry, kHashTimeOfDay, read_time)) {
                time = read_time;
            }

            const float dist = std::fabs(time - 0.5f);
            if (!found || dist < best_dist) {
                best_dist = dist;
                out_theme = theme;
                out_time = time;
                found = true;
            }
        }
        return found;
    }

    bool applyFirstWaterLikeRecord(WaterTheme& out_theme) const
    {
        for (size_t db = 0; db < views_.size(); ++db) {
            const GdbView& v = views_[db];
            if (!v.ok) continue;
            for (size_t record : v.record_data_offsets) {
                WaterTheme scratch = out_theme;
                WaterThemeRecordRef ref{db, record, true};
                if (applyThemeRecord(ref, scratch)) {
                    out_theme = scratch;
                    return true;
                }
            }
        }
        return false;
    }

    bool applyFirstSkyLikeRecord(SkyTheme& out_theme) const
    {
        for (size_t db = 0; db < views_.size(); ++db) {
            const GdbView& v = views_[db];
            if (!v.ok) continue;
            for (size_t record : v.record_data_offsets) {
                SkyTheme scratch = out_theme;
                WaterThemeRecordRef ref{db, record, true};
                if (applySkyThemeRecord(ref, scratch)) {
                    out_theme = scratch;
                    return true;
                }
            }
        }
        return false;
    }

    void finaliseCloudTheme(CloudTheme& theme) const
    {
        int count = 0;
        for (int i = 0; i < 4; ++i) {
            if (theme.layers[i].enabled) {
                ++count;
            }
        }
        theme.layer_count = count;
    }
};


}

bool ExtractWaterTheme(
    const std::vector<const std::vector<uint8_t>*>& gdbs,
    WaterTheme& out_theme)
{
    EnvironmentThemeExtractor extractor(gdbs);
    return extractor.extract(out_theme);
}

bool ExtractSkyTheme(
    const std::vector<const std::vector<uint8_t>*>& gdbs,
    SkyTheme& out_theme)
{
    EnvironmentThemeExtractor extractor(gdbs);
    return extractor.extractSky(out_theme);
}

bool ExtractCloudTheme(
    const std::vector<const std::vector<uint8_t>*>& gdbs,
    CloudTheme& out_theme)
{
    EnvironmentThemeExtractor extractor(gdbs);
    return extractor.extractClouds(out_theme);
}

bool ExtractWeatherTheme(
    const std::vector<const std::vector<uint8_t>*>& gdbs,
    WeatherTheme& out_theme)
{
    EnvironmentThemeExtractor extractor(gdbs);
    return extractor.extractWeather(out_theme);
}

bool ExtractEnvironmentThemeTimeline(
    const std::vector<const std::vector<uint8_t>*>& gdbs,
    EnvironmentThemeTimeline& out_timeline)
{
    EnvironmentThemeExtractor extractor(gdbs);
    return extractor.extractEnvironmentTimeline(out_timeline);
}

}
