struct StreamingModelCandidate {
    std::string hint_path;
    std::string resolved_path;
    std::string key;
    std::string path_key;
    std::string hint_lower;
    std::string resolved_lower;
    std::string display_name;
    const FlatAssetEntry* entry = nullptr;
    bool from_gmd = false;
    std::string gmd_bnk_path;
    int gmd_file_index = -1;
};

struct Vec3f {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Mat3f {
    float m[9] = {
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 1.0f,
    };
};

struct Xform3f {
    Mat3f r;
    Vec3f t;
};

struct GmdLayoutChild {
    std::string raw_path;
    std::string asset_key;
    std::string resolved_path;
    std::string resolved_key;
    Xform3f local;
    size_t offset = 0;
};

