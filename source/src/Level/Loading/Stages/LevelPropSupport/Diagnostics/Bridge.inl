std::vector<StreamingModelCandidate>
collect_streaming_model_candidates(const std::vector<std::string>& streaming_bnks);

std::string lower_slash(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

void bridge_debug_write(const std::string&, bool = false) {}

bool is_bridge_debug_path(const std::string& path)
{
    return lower_slash(path).find("bridge") != std::string::npos;
}

const char* bridge_block_origin(const Level::PropBlock& block)
{
    if (block.type == 2 || block.type == 21) return "engine_level";
    if (block.type == 0xB1) return "gdb_derived";
    if (block.type == 0xB2) return "heuristic_derived";
    if (block.type == 0xB3) return "editor_addition";
    return "other";
}

void bridge_debug_dump_blocks(
    const char* stage, const std::vector<Level::PropBlock>& blocks)
{
    std::ostringstream out;
    std::size_t bridge_blocks = 0;
    std::size_t bridge_instances = 0;
    out << "\n=== " << stage << " ===\n";
    for (const Level::PropBlock& block : blocks) {
        if (!is_bridge_debug_path(block.model_path) &&
            !is_bridge_debug_path(block.lod_model_path) &&
            !is_bridge_debug_path(block.shadow_model_path) &&
            !is_bridge_debug_path(block.extra_model_path)) {
            continue;
        }
        ++bridge_blocks;
        bridge_instances += block.instances.size();
        out << "BLOCK origin=" << bridge_block_origin(block)
            << " type=0x" << std::hex << block.type << std::dec
            << " block_offset=" << block.offset
            << " instances=" << block.instances.size()
            << "\n  model=" << block.model_path;
        if (!block.lod_model_path.empty()) {
            out << "\n  lod=" << block.lod_model_path;
        }
        if (!block.shadow_model_path.empty()) {
            out << "\n  shadow=" << block.shadow_model_path;
        }
        if (!block.extra_model_path.empty()) {
            out << "\n  extra=" << block.extra_model_path;
        }
        out << '\n';
        for (std::size_t i = 0; i < block.instances.size(); ++i) {
            const Level::PropInstance& instance = block.instances[i];
            out << "  [" << i << "] pos=("
                << instance.values[0] << ", "
                << instance.values[1] << ", "
                << instance.values[2] << ") scale=("
                << instance.values[9] << ", "
                << instance.values[10] << ", "
                << instance.values[11] << ") hash=0x"
                << std::hex << instance.hash
                << " gdb_entity=0x" << instance.gdb_entity_hash
                << std::dec << " lev_kind="
                << static_cast<unsigned>(instance.lev_rec_kind)
                << " record_offset=" << instance.record_file_offset
                << " pos_offset=" << instance.pos_file_offset << '\n';
        }
    }
    out << "SUMMARY bridge_blocks=" << bridge_blocks
        << " bridge_instances=" << bridge_instances << '\n';
    bridge_debug_write(out.str());
}

