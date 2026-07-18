constexpr std::array<const char*, 16> kControlFlowNames = {{
    "nop", "exec", "exec_end", "cexec", "cexec_end", "cexecp",
    "cexecp_end", "loop", "loop_end", "ccall", "ret", "cjmp",
    "alloc", "cexec", "cexec_end", "vfetche",
}};

constexpr std::array<const char*, 32> kFetchNames = {{
    "vfetch", "tfetch", "fetch3DNoiseMap", "fetchShadowMap",
    "fetchMultiSample", nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    "getCompTexLOD", "getBorderColorFraction", "getGradient",
    "getWeights", nullptr, nullptr, nullptr, nullptr,
    "setTexLOD", "setGradientH", "setGradientV", "setFilter4Weights",
    nullptr, nullptr, nullptr, nullptr,
}};

constexpr std::array<const char*, 32> kVectorNames = {{
    "add", "mul", "max", "min", "seq", "sgt", "sge", "sne",
    "frc", "trunc", "floor", "mad", "cndeq", "cndge", "cndgt",
    "dp4", "dp3", "dp2add", "cube", "max4", "setp_eq_push",
    "setp_ne_push", "setp_gt_push", "setp_ge_push", "kill_eq",
    "kill_gt", "kill_ge", "kill_ne", "dst", "maxa", "opcode_30",
    "opcode_31",
}};

constexpr std::array<std::uint8_t, 32> kVectorOperandCounts = {{
    2, 2, 2, 2, 2, 2, 2, 2,
    1, 1, 1, 3, 3, 3, 3, 2,
    2, 3, 2, 1, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 0, 0,
}};

constexpr std::array<const char*, 64> kScalarNames = {{
    "adds", "adds_prev", "muls", "muls_prev", "muls_prev2", "maxs",
    "mins", "seqs", "sgts", "sges", "snes", "frcs", "truncs",
    "floors", "exp", "logc", "log", "rcpc", "rcpf", "rcp", "rsqc",
    "rsqf", "rsq", "maxas", "maxasf", "subs", "subs_prev", "setp_eq",
    "setp_ne", "setp_gt", "setp_ge", "setp_inv", "setp_pop", "setp_clr",
    "setp_rstr", "kills_eq", "kills_gt", "kills_ge", "kills_ne",
    "kills_one", "sqrt", "opcode_41", "mulsc", "mulsc", "addsc",
    "addsc", "subsc", "subsc", "sin", "cos", "retain_prev",
    "opcode_51", "opcode_52", "opcode_53", "opcode_54", "opcode_55",
    "opcode_56", "opcode_57", "opcode_58", "opcode_59", "opcode_60",
    "opcode_61", "opcode_62", "opcode_63",
}};

constexpr std::array<std::uint8_t, 64> kScalarOperandCounts = {{
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 0, 1, 1, 1, 1, 1, 1,
    1, 0, 2, 2, 2, 2, 2, 2,
    1, 1, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
}};

constexpr std::array<std::uint8_t, 64> kScalarDisplayComponentCounts = {{
    2, 1, 2, 1, 2, 2, 2, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 2,
    2, 2, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
}};
