void write_matrix_json(std::ostream& os, const std::array<float, 16>& m)
{
    os << "[";
    for (size_t i = 0; i < m.size(); ++i) {
        if (i) os << ",";
        os << m[i];
    }
    os << "]";
}
