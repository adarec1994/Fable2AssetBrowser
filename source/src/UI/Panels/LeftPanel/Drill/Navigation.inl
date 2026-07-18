void drill_back(DrillState& d) {
    d.target_t = 0.0f;

}

bool drill_settled(const DrillState& d) {
    return std::abs(d.anim_t - d.target_t) < 0.001f;
}

