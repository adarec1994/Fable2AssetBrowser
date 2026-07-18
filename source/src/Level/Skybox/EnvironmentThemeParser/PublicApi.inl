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
