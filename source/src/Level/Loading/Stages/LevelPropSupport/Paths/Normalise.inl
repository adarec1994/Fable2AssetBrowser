std::vector<StreamingModelCandidate>
collect_streaming_model_candidates(const std::vector<std::string>& streaming_bnks);

std::string lower_slash(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

bool is_bridge_path(const std::string& path)
{
    return lower_slash(path).find("bridge") != std::string::npos;
}
