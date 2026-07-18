#include "lua_decompile.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace lua {

namespace {

#include "Decompiler/Core/Types.inl"
#include "Decompiler/Model/Runtime.inl"
#include "Decompiler/Expressions/Nodes.inl"
#include "Decompiler/Statements/Operations.inl"
#include "Decompiler/ControlFlow/Branches.inl"
#include "Decompiler/ControlFlow/Blocks.inl"
#include "Decompiler/Analysis/Variables.inl"
#include "Decompiler/Decompiler/Definition.inl"
#include "Decompiler/Output/Expressions.inl"
#include "Decompiler/Output/Blocks.inl"
#include "Decompiler/Processing/Instructions.inl"
#include "Decompiler/Processing/Sequences.inl"
#include "Decompiler/Processing/Branches.inl"
#include "Decompiler/Processing/Conditions.inl"

}

extern std::string run_full_decompiler(const LFunction& main,
                                       const CodeExtract& ex,
                                       const OpcodeMap& opcodes);

std::string run_full_decompiler(const LFunction& main,
                                const CodeExtract& ex,
                                const OpcodeMap& opcodes) {
    try {
        Decompiler dc(main, ex, opcodes, nullptr, 0);
        dc.decompile();
        Output out;
        dc.print(out);
        std::string result = out.str();
        if (result.empty()) result = "-- (decompiled to empty body)\n";
        return result;
    } catch (const std::exception& e) {
        return std::string("-- decompiler exception: ") + e.what() + "\n";
    } catch (...) {
        return "-- decompiler exception: unknown\n";
    }
}

}
