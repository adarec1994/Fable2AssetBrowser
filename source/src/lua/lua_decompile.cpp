#include "lua_decompile.h"

#include <sstream>
#include <unordered_map>

namespace lua {

namespace {

struct OpInfo { Op op; const char* name; OpcodeFormat fmt; };
static const OpInfo kOpTable[] = {
    { Op::MOVE,        "MOVE",      OpcodeFormat::A_B   },
    { Op::LOADK,       "LOADK",     OpcodeFormat::A_Bx  },
    { Op::LOADBOOL,    "LOADBOOL",  OpcodeFormat::A_B_C },
    { Op::LOADNIL,     "LOADNIL",   OpcodeFormat::A_B   },
    { Op::GETUPVAL,    "GETUPVAL",  OpcodeFormat::A_B   },
    { Op::GETGLOBAL,   "GETGLOBAL", OpcodeFormat::A_Bx  },
    { Op::GETTABLE,    "GETTABLE",  OpcodeFormat::A_B_C },
    { Op::SETGLOBAL,   "SETGLOBAL", OpcodeFormat::A_Bx  },
    { Op::SETUPVAL,    "SETUPVAL",  OpcodeFormat::A_B   },
    { Op::SETTABLE,    "SETTABLE",  OpcodeFormat::A_B_C },
    { Op::NEWTABLE,    "NEWTABLE",  OpcodeFormat::A_B_C },
    { Op::SELF,        "SELF",      OpcodeFormat::A_B_C },
    { Op::ADD,         "ADD",       OpcodeFormat::A_B_C },
    { Op::SUB,         "SUB",       OpcodeFormat::A_B_C },
    { Op::MUL,         "MUL",       OpcodeFormat::A_B_C },
    { Op::DIV,         "DIV",       OpcodeFormat::A_B_C },
    { Op::MOD,         "MOD",       OpcodeFormat::A_B_C },
    { Op::POW,         "POW",       OpcodeFormat::A_B_C },
    { Op::UNM,         "UNM",       OpcodeFormat::A_B   },
    { Op::NOT,         "NOT",       OpcodeFormat::A_B   },
    { Op::LEN,         "LEN",       OpcodeFormat::A_B   },
    { Op::CONCAT,      "CONCAT",    OpcodeFormat::A_B_C },
    { Op::JMP,         "JMP",       OpcodeFormat::sBx   },
    { Op::EQ,          "EQ",        OpcodeFormat::A_B_C },
    { Op::LT,          "LT",        OpcodeFormat::A_B_C },
    { Op::LE,          "LE",        OpcodeFormat::A_B_C },
    { Op::TEST,        "TEST",      OpcodeFormat::A_C   },
    { Op::TESTSET,     "TESTSET",   OpcodeFormat::A_B_C },
    { Op::CALL,        "CALL",      OpcodeFormat::A_B_C },
    { Op::TAILCALL,    "TAILCALL",  OpcodeFormat::A_B_C },
    { Op::RETURN,      "RETURN",    OpcodeFormat::A_B   },
    { Op::FORLOOP,     "FORLOOP",   OpcodeFormat::A_sBx },
    { Op::FORPREP,     "FORPREP",   OpcodeFormat::A_sBx },
    { Op::TFORLOOP,    "TFORLOOP",  OpcodeFormat::A_C   },
    { Op::SETLIST,     "SETLIST",   OpcodeFormat::A_B_C },
    { Op::CLOSE,       "CLOSE",     OpcodeFormat::A     },
    { Op::CLOSURE,     "CLOSURE",   OpcodeFormat::A_Bx  },
    { Op::VARARG,      "VARARG",    OpcodeFormat::A_B   },
    { Op::LOADKX,      "LOADKX",    OpcodeFormat::A     },
    { Op::GETTABUP,    "GETTABUP",  OpcodeFormat::A_B_C },
    { Op::SETTABUP,    "SETTABUP",  OpcodeFormat::A_B_C },
    { Op::TFORCALL,    "TFORCALL",  OpcodeFormat::A_C   },
    { Op::EXTRAARG,    "EXTRAARG",  OpcodeFormat::Ax    },
    { Op::NEWTABLE50,  "NEWTABLE50",OpcodeFormat::A_B_C },
    { Op::SETLIST50,   "SETLIST50", OpcodeFormat::A_B_C },
    { Op::SETLISTO,    "SETLISTO",  OpcodeFormat::A_Bx  },
    { Op::TFORPREP,    "TFORPREP",  OpcodeFormat::A_sBx },
    { Op::TEST50,      "TEST50",    OpcodeFormat::A_B_C },
    { Op::IDIV,        "IDIV",      OpcodeFormat::A_B_C },
    { Op::BAND,        "BAND",      OpcodeFormat::A_B_C },
    { Op::BOR,         "BOR",       OpcodeFormat::A_B_C },
    { Op::BXOR,        "BXOR",      OpcodeFormat::A_B_C },
    { Op::SHL,         "SHL",       OpcodeFormat::A_B_C },
    { Op::SHR,         "SHR",       OpcodeFormat::A_B_C },
    { Op::BNOT,        "BNOT",      OpcodeFormat::A_B   },
};

const OpInfo* find_op_info(Op op) {
    for (auto& e : kOpTable) if (e.op == op) return &e;
    return nullptr;
}

}

const char* op_name(Op op) {
    if (auto* e = find_op_info(op)) return e->name;
    return "UNKNOWN";
}
OpcodeFormat op_format(Op op) {
    if (auto* e = find_op_info(op)) return e->fmt;
    return OpcodeFormat::NONE;
}

OpcodeMap::OpcodeMap(int versionNumber) {
    if (versionNumber == 0x51) {
        map_ = {
            Op::MOVE,     Op::LOADK,    Op::LOADBOOL, Op::LOADNIL,
            Op::GETUPVAL, Op::GETGLOBAL,Op::GETTABLE, Op::SETGLOBAL,
            Op::SETUPVAL, Op::SETTABLE, Op::NEWTABLE, Op::SELF,
            Op::ADD,      Op::SUB,      Op::MUL,      Op::DIV,
            Op::MOD,      Op::POW,      Op::UNM,      Op::NOT,
            Op::LEN,      Op::CONCAT,   Op::JMP,      Op::EQ,
            Op::LT,       Op::LE,       Op::TEST,     Op::TESTSET,
            Op::CALL,     Op::TAILCALL, Op::RETURN,   Op::FORLOOP,
            Op::FORPREP,  Op::TFORLOOP, Op::SETLIST,  Op::CLOSE,
            Op::CLOSURE,  Op::VARARG,
        };
    }
}

Op OpcodeMap::get(int n) const {
    if (n >= 0 && n < (int)map_.size()) return map_[(size_t)n];
    return Op::UNKNOWN;
}

std::string codepoint_to_string(Op op, uint32_t cp, const CodeExtract& ex) {
    std::ostringstream os;
    os << op_name(op);
    switch (op_format(op)) {
        case OpcodeFormat::A:     os << " " << ex.extract_A(cp); break;
        case OpcodeFormat::A_B:   os << " " << ex.extract_A(cp) << " " << ex.extract_B(cp); break;
        case OpcodeFormat::A_C:   os << " " << ex.extract_A(cp) << " " << ex.extract_C(cp); break;
        case OpcodeFormat::A_B_C: os << " " << ex.extract_A(cp) << " " << ex.extract_B(cp) << " " << ex.extract_C(cp); break;
        case OpcodeFormat::A_Bx:  os << " " << ex.extract_A(cp) << " " << ex.extract_Bx(cp); break;
        case OpcodeFormat::A_sBx: os << " " << ex.extract_A(cp) << " " << ex.extract_sBx(cp); break;
        case OpcodeFormat::sBx:   os << " " << ex.extract_sBx(cp); break;
        case OpcodeFormat::Ax:    os << " <Ax>"; break;
        case OpcodeFormat::NONE:  break;
    }
    return os.str();
}

namespace {

std::string constant_to_string(const LObject& c) {
    switch (c.tag) {
        case LObject::NIL:    return "nil";
        case LObject::BOOL:   return c.b ? "true" : "false";
        case LObject::NUMBER: {
            std::ostringstream os; os.precision(14);
            if ((double)(int64_t)c.n == c.n) os << (int64_t)c.n;
            else                              os << c.n;
            return os.str();
        }
        case LObject::STRING: {
            std::string out = "\"";
            for (unsigned char ch : c.s.value) {
                switch (ch) {
                    case '"':  out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\n': out += "\\n";  break;
                    case '\r': out += "\\r";  break;
                    case '\t': out += "\\t";  break;
                    default:
                        if (ch < 32 || ch > 126) {
                            char buf[8];
                            std::snprintf(buf, sizeof(buf), "\\%u", (unsigned)ch);
                            out += buf;
                        } else {
                            out += (char)ch;
                        }
                }
            }
            return out + "\"";
        }
    }
    return "?";
}

void disassemble_function(std::ostringstream& os,
                          const LFunction& f,
                          const CodeExtract& ex,
                          const OpcodeMap& opcodes,
                          int depth,
                          int& counter) {
    int my_id = counter++;
    std::string indent(depth * 2, ' ');
    os << indent << "-- function #" << my_id;
    if (!f.sourceName.empty()) os << " (source=\"" << f.sourceName << "\")";
    os << "  lines=" << f.lineBegin << ".." << f.lineEnd
       << "  params=" << f.numParams
       << (f.vararg ? " vararg" : "")
       << "  stack=" << f.maximumStackSize
       << "  upvalues=" << f.numUpvalues
       << "\n";

    if (!f.constants.empty()) {
        os << indent << "-- constants (" << f.constants.size() << "):\n";
        for (size_t i = 0; i < f.constants.size(); i++) {
            os << indent << "  [" << i << "] = " << constant_to_string(f.constants[i]) << "\n";
        }
    }
    if (!f.locals.empty()) {
        os << indent << "-- locals (" << f.locals.size() << "):\n";
        for (size_t i = 0; i < f.locals.size(); i++) {
            os << indent << "  [" << i << "] " << f.locals[i].name
               << "  pc " << f.locals[i].start << ".." << f.locals[i].end << "\n";
        }
    }
    if (!f.upvalues.empty()) {
        os << indent << "-- upvalues (" << f.upvalues.size() << "):\n";
        for (size_t i = 0; i < f.upvalues.size(); i++) {
            os << indent << "  [" << i << "] " << (f.upvalues[i].name.empty() ? "<anon>" : f.upvalues[i].name) << "\n";
        }
    }

    os << indent << "-- code (" << f.code.size() << " instructions):\n";
    for (size_t pc = 0; pc < f.code.size(); pc++) {
        uint32_t cp = f.code[pc];
        Op op = opcodes.get(ex.extract_op(cp));
        char head[16];
        std::snprintf(head, sizeof(head), "%4zu: ", pc);
        os << indent << head << codepoint_to_string(op, cp, ex) << "\n";
    }

    for (auto& child : f.functions) {
        os << "\n";
        disassemble_function(os, *child, ex, opcodes, depth + 1, counter);
    }
}

}

std::string disassemble(const LFunction& main, const CodeExtract& ex,
                        const OpcodeMap& opcodes) {
    std::ostringstream os;
    int counter = 0;
    disassemble_function(os, main, ex, opcodes, 0, counter);
    return os.str();
}

extern std::string run_full_decompiler(const LFunction& main,
                                       const CodeExtract& ex,
                                       const OpcodeMap& opcodes);

std::string decompile_lua_bytecode(const uint8_t* data, size_t size) {
    try {
        BHeader hdr = parse_lua_bytecode(data, size);
        std::string out = run_full_decompiler(*hdr.main, *hdr.extractor, *hdr.opcodes);
        if (!out.empty()) return out;
        return disassemble(*hdr.main, *hdr.extractor, *hdr.opcodes);
    } catch (const std::exception& e) {
        return std::string("-- lua decompiler error: ") + e.what() + "\n";
    } catch (...) {
        return "-- lua decompiler error: unknown exception during parse\n";
    }
}

}
