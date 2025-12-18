#include "Lua.h"
#include "Progress.h"
#include "State.h"
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <cstring>
#include <thread>
#include <map>
#include <stack>

namespace lua51 {

enum OpCode {
    OP_MOVE, OP_LOADK, OP_LOADBOOL, OP_LOADNIL,
    OP_GETUPVAL, OP_GETGLOBAL, OP_GETTABLE,
    OP_SETGLOBAL, OP_SETUPVAL, OP_SETTABLE,
    OP_NEWTABLE, OP_SELF,
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_POW, OP_UNM, OP_NOT, OP_LEN,
    OP_CONCAT,
    OP_JMP,
    OP_EQ, OP_LT, OP_LE,
    OP_TEST, OP_TESTSET,
    OP_CALL, OP_TAILCALL, OP_RETURN,
    OP_FORLOOP, OP_FORPREP,
    OP_TFORLOOP,
    OP_SETLIST,
    OP_CLOSE, OP_CLOSURE,
    OP_VARARG,
    OP_MAX
};

static const char* opnames[] = {
    "MOVE", "LOADK", "LOADBOOL", "LOADNIL",
    "GETUPVAL", "GETGLOBAL", "GETTABLE",
    "SETGLOBAL", "SETUPVAL", "SETTABLE",
    "NEWTABLE", "SELF",
    "ADD", "SUB", "MUL", "DIV", "MOD", "POW", "UNM", "NOT", "LEN",
    "CONCAT",
    "JMP",
    "EQ", "LT", "LE",
    "TEST", "TESTSET",
    "CALL", "TAILCALL", "RETURN",
    "FORLOOP", "FORPREP",
    "TFORLOOP",
    "SETLIST",
    "CLOSE", "CLOSURE",
    "VARARG"
};

#define GET_OPCODE(i) ((OpCode)((i) & 0x3F))
#define GETARG_A(i)   (((i) >> 6) & 0xFF)
#define GETARG_B(i)   (((i) >> 23) & 0x1FF)
#define GETARG_C(i)   (((i) >> 14) & 0x1FF)
#define GETARG_Bx(i)  (((i) >> 14) & 0x3FFFF)
#define GETARG_sBx(i) (GETARG_Bx(i) - 131071)
#define ISK(x)        ((x) & 0x100)
#define INDEXK(x)     ((x) & 0xFF)

enum {
    LUA_TNIL = 0,
    LUA_TBOOLEAN = 1,
    LUA_TNUMBER = 3,
    LUA_TSTRING = 4
};

struct Constant {
    int type;
    bool bval;
    double nval;
    std::string sval;
};

struct Local {
    std::string name;
    int startpc;
    int endpc;
};

struct Upvalue {
    std::string name;
};

struct Function {
    std::string source;
    int linedefined;
    int lastlinedefined;
    uint8_t nups;
    uint8_t numparams;
    uint8_t is_vararg;
    uint8_t maxstacksize;
    std::vector<uint32_t> code;
    std::vector<Constant> constants;
    std::vector<Function> protos;
    std::vector<int> lineinfo;
    std::vector<Local> locals;
    std::vector<Upvalue> upvalues;
};

class BytecodeReader {
public:
    const uint8_t* data;
    size_t size;
    size_t pos;
    bool little_endian;
    int sizeof_int;
    int sizeof_size_t;
    int sizeof_instruction;
    int sizeof_number;
    bool integral_number;

    BytecodeReader(const uint8_t* d, size_t s) : data(d), size(s), pos(0) {
        little_endian = true;
        sizeof_int = 4;
        sizeof_size_t = 4;
        sizeof_instruction = 4;
        sizeof_number = 8;
        integral_number = false;
    }

    uint8_t read_byte() {
        if (pos >= size) return 0;
        return data[pos++];
    }

    uint32_t read_int() {
        uint32_t v = 0;
        for (int i = 0; i < sizeof_int; i++) {
            v |= ((uint32_t)read_byte()) << (i * 8);
        }
        return v;
    }

    size_t read_size() {
        size_t v = 0;
        for (int i = 0; i < sizeof_size_t; i++) {
            v |= ((size_t)read_byte()) << (i * 8);
        }
        return v;
    }

    double read_number() {
        if (sizeof_number == 8) {
            uint64_t v = 0;
            for (int i = 0; i < 8; i++) {
                v |= ((uint64_t)read_byte()) << (i * 8);
            }
            double d;
            memcpy(&d, &v, 8);
            return d;
        } else {
            uint32_t v = 0;
            for (int i = 0; i < 4; i++) {
                v |= ((uint32_t)read_byte()) << (i * 8);
            }
            float f;
            memcpy(&f, &v, 4);
            return f;
        }
    }

    std::string read_string() {
        size_t len = read_size();
        if (len == 0) return "";
        std::string s;
        s.reserve(len - 1);
        for (size_t i = 0; i < len - 1; i++) {
            s += (char)read_byte();
        }
        read_byte(); // null terminator
        return s;
    }

    bool read_header() {
        // Signature: 0x1B 'L' 'u' 'a'
        if (read_byte() != 0x1B) return false;
        if (read_byte() != 'L') return false;
        if (read_byte() != 'u') return false;
        if (read_byte() != 'a') return false;

        // Version (0x51 for Lua 5.1)
        uint8_t version = read_byte();
        if (version != 0x51) return false;

        // Format (0 = official)
        read_byte();

        // Endianness (1 = little endian)
        little_endian = read_byte() == 1;

        // Sizes
        sizeof_int = read_byte();
        sizeof_size_t = read_byte();
        sizeof_instruction = read_byte();
        sizeof_number = read_byte();
        integral_number = read_byte() != 0;

        return true;
    }

    Function read_function() {
        Function f;
        f.source = read_string();
        f.linedefined = read_int();
        f.lastlinedefined = read_int();
        f.nups = read_byte();
        f.numparams = read_byte();
        f.is_vararg = read_byte();
        f.maxstacksize = read_byte();

        // Code
        int sizecode = read_int();
        f.code.resize(sizecode);
        for (int i = 0; i < sizecode; i++) {
            f.code[i] = read_int();
        }

        // Constants
        int sizek = read_int();
        f.constants.resize(sizek);
        for (int i = 0; i < sizek; i++) {
            Constant& k = f.constants[i];
            k.type = read_byte();
            switch (k.type) {
                case LUA_TNIL:
                    break;
                case LUA_TBOOLEAN:
                    k.bval = read_byte() != 0;
                    break;
                case LUA_TNUMBER:
                    k.nval = read_number();
                    break;
                case LUA_TSTRING:
                    k.sval = read_string();
                    break;
            }
        }

        // Protos (nested functions)
        int sizep = read_int();
        f.protos.resize(sizep);
        for (int i = 0; i < sizep; i++) {
            f.protos[i] = read_function();
        }

        // Line info (debug)
        int sizelineinfo = read_int();
        f.lineinfo.resize(sizelineinfo);
        for (int i = 0; i < sizelineinfo; i++) {
            f.lineinfo[i] = read_int();
        }

        // Locals (debug)
        int sizelocvars = read_int();
        f.locals.resize(sizelocvars);
        for (int i = 0; i < sizelocvars; i++) {
            f.locals[i].name = read_string();
            f.locals[i].startpc = read_int();
            f.locals[i].endpc = read_int();
        }

        // Upvalues (debug)
        int sizeupvalues = read_int();
        f.upvalues.resize(sizeupvalues);
        for (int i = 0; i < sizeupvalues; i++) {
            f.upvalues[i].name = read_string();
        }

        return f;
    }
};

class Decompiler {
public:
    const Function* f;
    std::vector<std::string> regs;
    std::vector<bool> reg_is_local;
    std::vector<int> reg_priority;
    std::stringstream out;
    int indent;
    int pc;
    std::vector<int> pending_ends;

    Decompiler(const Function* func) : f(func), indent(0), pc(0) {
        regs.resize(256);
        reg_is_local.resize(256, false);
        reg_priority.resize(256, 0);
        for (int i = 0; i < f->numparams; i++) {
            regs[i] = get_local_name(i);
            reg_is_local[i] = true;
        }
    }

    std::string escape_string(const std::string& s) {
        std::string result = "\"";
        for (unsigned char c : s) {
            switch (c) {
                case '"': result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default:
                    if (c < 32 || c > 126) {
                        char buf[8];
                        snprintf(buf, sizeof(buf), "\\%d", c);
                        result += buf;
                    } else {
                        result += c;
                    }
            }
        }
        result += "\"";
        return result;
    }

    std::string constant_to_string(int idx) {
        if (idx < 0 || idx >= (int)f->constants.size()) {
            return "nil";
        }
        const Constant& k = f->constants[idx];
        switch (k.type) {
            case LUA_TNIL: return "nil";
            case LUA_TBOOLEAN: return k.bval ? "true" : "false";
            case LUA_TNUMBER: {
                char buf[64];
                if (k.nval == (int64_t)k.nval) {
                    snprintf(buf, sizeof(buf), "%lld", (long long)k.nval);
                } else {
                    snprintf(buf, sizeof(buf), "%.14g", k.nval);
                }
                return buf;
            }
            case LUA_TSTRING: return escape_string(k.sval);
            default: return "nil";
        }
    }

    std::string get_global_name(int idx) {
        if (idx >= 0 && idx < (int)f->constants.size() && f->constants[idx].type == LUA_TSTRING) {
            return f->constants[idx].sval;
        }
        char buf[32];
        snprintf(buf, sizeof(buf), "_G[%d]", idx);
        return buf;
    }

    std::string rk(int r) {
        if (ISK(r)) {
            return constant_to_string(INDEXK(r));
        }
        return reg(r);
    }

    std::string table_key(int r) {
        if (ISK(r)) {
            int idx = INDEXK(r);
            if (idx >= 0 && idx < (int)f->constants.size()) {
                const Constant& k = f->constants[idx];
                if (k.type == LUA_TSTRING) {
                    bool valid_id = !k.sval.empty() && (isalpha(k.sval[0]) || k.sval[0] == '_');
                    for (size_t i = 1; valid_id && i < k.sval.size(); i++) {
                        valid_id = isalnum(k.sval[i]) || k.sval[i] == '_';
                    }
                    if (valid_id) {
                        return "." + k.sval;
                    }
                }
            }
            return "[" + constant_to_string(idx) + "]";
        }
        return "[" + reg(r) + "]";
    }

    std::string reg(int r) {
        for (size_t i = 0; i < f->locals.size(); i++) {
            if ((int)i == r && pc >= f->locals[i].startpc && pc <= f->locals[i].endpc) {
                return f->locals[i].name;
            }
        }
        if (!regs[r].empty()) {
            return regs[r];
        }
        char buf[16];
        snprintf(buf, sizeof(buf), "r%d", r);
        return buf;
    }

    void set_reg(int r, const std::string& val, int prio = 0) {
        regs[r] = val;
        reg_priority[r] = prio;
    }

    std::string get_local_name(int r) {
        for (size_t i = 0; i < f->locals.size(); i++) {
            if ((int)i == r) {
                return f->locals[i].name;
            }
        }
        char buf[16];
        snprintf(buf, sizeof(buf), "local_%d", r);
        return buf;
    }

    std::string upvalue(int r) {
        if (r < (int)f->upvalues.size() && !f->upvalues[r].name.empty()) {
            return f->upvalues[r].name;
        }
        char buf[16];
        snprintf(buf, sizeof(buf), "upval_%d", r);
        return buf;
    }

    void write_indent() {
        for (int i = 0; i < indent; i++) {
            out << "\t";
        }
    }

    void check_pending_ends() {
        while (!pending_ends.empty() && pending_ends.back() <= pc) {
            pending_ends.pop_back();
            indent--;
            write_indent();
            out << "end\n";
        }
    }

    void decompile() {
        if (f->linedefined > 0) {
            out << "function(";
            for (int i = 0; i < f->numparams; i++) {
                if (i > 0) out << ", ";
                out << get_local_name(i);
            }
            if (f->is_vararg) {
                if (f->numparams > 0) out << ", ";
                out << "...";
            }
            out << ")\n";
            indent++;
        }

        decompile_block(0, (int)f->code.size());

        while (!pending_ends.empty()) {
            pending_ends.pop_back();
            indent--;
            write_indent();
            out << "end\n";
        }

        if (f->linedefined > 0) {
            indent--;
            write_indent();
            out << "end";
        }
    }

    void decompile_block(int start, int end) {
        for (pc = start; pc < end; pc++) {
            check_pending_ends();

            uint32_t i = f->code[pc];
            OpCode op = GET_OPCODE(i);
            int a = GETARG_A(i);
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            int bx = GETARG_Bx(i);
            int sbx = GETARG_sBx(i);

            switch (op) {
                case OP_MOVE: {
                    std::string src = reg(b);
                    set_reg(a, src);
                    write_indent();
                    out << reg(a) << " = " << src << "\n";
                    break;
                }

                case OP_LOADK: {
                    std::string val = constant_to_string(bx);
                    set_reg(a, val);
                    write_indent();
                    out << reg(a) << " = " << val << "\n";
                    break;
                }

                case OP_LOADBOOL: {
                    std::string val = b ? "true" : "false";
                    set_reg(a, val);
                    write_indent();
                    out << reg(a) << " = " << val << "\n";
                    if (c) pc++;
                    break;
                }

                case OP_LOADNIL: {
                    write_indent();
                    for (int r = a; r <= b; r++) {
                        set_reg(r, "nil");
                        if (r > a) out << ", ";
                        out << reg(r);
                    }
                    out << " = nil\n";
                    break;
                }

                case OP_GETUPVAL: {
                    std::string val = upvalue(b);
                    set_reg(a, val);
                    write_indent();
                    out << reg(a) << " = " << val << "\n";
                    break;
                }

                case OP_GETGLOBAL: {
                    std::string gname = get_global_name(bx);
                    set_reg(a, gname);
                    write_indent();
                    out << reg(a) << " = " << gname << "\n";
                    break;
                }

                case OP_GETTABLE: {
                    std::string key = table_key(c);
                    std::string expr = reg(b) + key;
                    set_reg(a, expr);
                    write_indent();
                    out << reg(a) << " = " << expr << "\n";
                    break;
                }

                case OP_SETGLOBAL: {
                    std::string gname = get_global_name(bx);
                    write_indent();
                    out << gname << " = " << reg(a) << "\n";
                    break;
                }

                case OP_SETUPVAL:
                    write_indent();
                    out << upvalue(b) << " = " << reg(a) << "\n";
                    break;

                case OP_SETTABLE: {
                    std::string key = table_key(b);
                    write_indent();
                    out << reg(a) << key << " = " << rk(c) << "\n";
                    break;
                }

                case OP_NEWTABLE: {
                    set_reg(a, "{}");
                    write_indent();
                    out << reg(a) << " = {}\n";
                    break;
                }

                case OP_SELF: {
                    std::string obj = reg(b);
                    std::string key = table_key(c);
                    set_reg(a + 1, obj);
                    set_reg(a, obj + key);
                    write_indent();
                    out << reg(a + 1) << " = " << obj << "\n";
                    write_indent();
                    out << reg(a) << " = " << obj << key << "\n";
                    break;
                }

                case OP_ADD: {
                    std::string expr = rk(b) + " + " + rk(c);
                    set_reg(a, expr, 4);
                    write_indent();
                    out << reg(a) << " = " << expr << "\n";
                    break;
                }
                case OP_SUB: {
                    std::string expr = rk(b) + " - " + rk(c);
                    set_reg(a, expr, 4);
                    write_indent();
                    out << reg(a) << " = " << expr << "\n";
                    break;
                }
                case OP_MUL: {
                    std::string expr = rk(b) + " * " + rk(c);
                    set_reg(a, expr, 5);
                    write_indent();
                    out << reg(a) << " = " << expr << "\n";
                    break;
                }
                case OP_DIV: {
                    std::string expr = rk(b) + " / " + rk(c);
                    set_reg(a, expr, 5);
                    write_indent();
                    out << reg(a) << " = " << expr << "\n";
                    break;
                }
                case OP_MOD: {
                    std::string expr = rk(b) + " % " + rk(c);
                    set_reg(a, expr, 5);
                    write_indent();
                    out << reg(a) << " = " << expr << "\n";
                    break;
                }
                case OP_POW: {
                    std::string expr = rk(b) + " ^ " + rk(c);
                    set_reg(a, expr, 7);
                    write_indent();
                    out << reg(a) << " = " << expr << "\n";
                    break;
                }

                case OP_UNM: {
                    std::string expr = "-" + reg(b);
                    set_reg(a, expr, 6);
                    write_indent();
                    out << reg(a) << " = " << expr << "\n";
                    break;
                }
                case OP_NOT: {
                    std::string expr = "not " + reg(b);
                    set_reg(a, expr, 6);
                    write_indent();
                    out << reg(a) << " = " << expr << "\n";
                    break;
                }
                case OP_LEN: {
                    std::string expr = "#" + reg(b);
                    set_reg(a, expr, 6);
                    write_indent();
                    out << reg(a) << " = " << expr << "\n";
                    break;
                }

                case OP_CONCAT: {
                    std::string expr;
                    for (int r = b; r <= c; r++) {
                        if (r > b) expr += " .. ";
                        expr += reg(r);
                    }
                    set_reg(a, expr, 3);
                    write_indent();
                    out << reg(a) << " = " << expr << "\n";
                    break;
                }

                case OP_JMP:
                    break;

                case OP_EQ: {
                    int dest = pc + 2 + GETARG_sBx(f->code[pc + 1]);
                    write_indent();
                    if (a) {
                        out << "if " << rk(b) << " == " << rk(c) << " then\n";
                    } else {
                        out << "if " << rk(b) << " ~= " << rk(c) << " then\n";
                    }
                    indent++;
                    pending_ends.push_back(dest - 1);
                    pc++;
                    break;
                }
                case OP_LT: {
                    int dest = pc + 2 + GETARG_sBx(f->code[pc + 1]);
                    write_indent();
                    if (a) {
                        out << "if " << rk(b) << " < " << rk(c) << " then\n";
                    } else {
                        out << "if " << rk(b) << " >= " << rk(c) << " then\n";
                    }
                    indent++;
                    pending_ends.push_back(dest - 1);
                    pc++;
                    break;
                }
                case OP_LE: {
                    int dest = pc + 2 + GETARG_sBx(f->code[pc + 1]);
                    write_indent();
                    if (a) {
                        out << "if " << rk(b) << " <= " << rk(c) << " then\n";
                    } else {
                        out << "if " << rk(b) << " > " << rk(c) << " then\n";
                    }
                    indent++;
                    pending_ends.push_back(dest - 1);
                    pc++;
                    break;
                }

                case OP_TEST: {
                    int dest = pc + 2 + GETARG_sBx(f->code[pc + 1]);
                    write_indent();
                    if (c) {
                        out << "if " << reg(a) << " then\n";
                    } else {
                        out << "if not " << reg(a) << " then\n";
                    }
                    indent++;
                    pending_ends.push_back(dest - 1);
                    pc++;
                    break;
                }

                case OP_TESTSET: {
                    int dest = pc + 2 + GETARG_sBx(f->code[pc + 1]);
                    write_indent();
                    if (c) {
                        out << "if " << reg(b) << " then\n";
                        indent++;
                        write_indent();
                        out << reg(a) << " = " << reg(b) << "\n";
                    } else {
                        out << "if not " << reg(b) << " then\n";
                        indent++;
                        write_indent();
                        out << reg(a) << " = " << reg(b) << "\n";
                    }
                    pending_ends.push_back(dest - 1);
                    pc++;
                    break;
                }

                case OP_CALL: {
                    std::string func = reg(a);
                    std::string args;
                    if (b == 0) {
                        args = "...";
                    } else if (b == 1) {
                        args = "";
                    } else {
                        for (int r = a + 1; r < a + b; r++) {
                            if (r > a + 1) args += ", ";
                            args += reg(r);
                        }
                    }

                    std::string call_expr = func + "(" + args + ")";

                    if (c == 0 || c == 1) {
                        write_indent();
                        out << call_expr << "\n";
                    } else {
                        write_indent();
                        for (int r = a; r < a + c - 1; r++) {
                            if (r > a) out << ", ";
                            out << reg(r);
                            set_reg(r, "");
                        }
                        out << " = " << call_expr << "\n";
                    }
                    break;
                }

                case OP_TAILCALL: {
                    std::string func = reg(a);
                    std::string args;
                    if (b == 0) {
                        args = "...";
                    } else if (b == 1) {
                        args = "";
                    } else {
                        for (int r = a + 1; r < a + b; r++) {
                            if (r > a + 1) args += ", ";
                            args += reg(r);
                        }
                    }
                    write_indent();
                    out << "return " << func << "(" << args << ")\n";
                    break;
                }

                case OP_RETURN: {
                    write_indent();
                    if (b == 0) {
                        out << "return ...\n";
                    } else if (b == 1) {
                        out << "return\n";
                    } else {
                        out << "return ";
                        for (int r = a; r < a + b - 1; r++) {
                            if (r > a) out << ", ";
                            out << reg(r);
                        }
                        out << "\n";
                    }
                    break;
                }

                case OP_FORLOOP: {
                    indent--;
                    write_indent();
                    out << "end\n";
                    break;
                }

                case OP_FORPREP: {
                    std::string var = get_local_name(a + 3);
                    write_indent();
                    out << "for " << var << " = " << reg(a) << ", " << reg(a + 1);
                    std::string step = reg(a + 2);
                    if (step != "1") {
                        out << ", " << step;
                    }
                    out << " do\n";
                    indent++;
                    break;
                }

                case OP_TFORLOOP: {
                    int nresults = c;
                    write_indent();
                    for (int r = a + 3; r <= a + 2 + nresults; r++) {
                        if (r > a + 3) out << ", ";
                        out << get_local_name(r);
                    }
                    out << " = " << reg(a) << "(" << reg(a + 1) << ", " << reg(a + 2) << ")\n";
                    write_indent();
                    out << "if " << get_local_name(a + 3) << " ~= nil then\n";
                    indent++;
                    write_indent();
                    out << reg(a + 2) << " = " << get_local_name(a + 3) << "\n";
                    indent--;
                    write_indent();
                    out << "else break end\n";
                    break;
                }

                case OP_SETLIST: {
                    int start_idx = (c - 1) * 50;
                    int count = (b == 0) ? 50 : b;
                    for (int r = 1; r <= count; r++) {
                        write_indent();
                        out << reg(a) << "[" << (start_idx + r) << "] = " << reg(a + r) << "\n";
                    }
                    break;
                }

                case OP_CLOSE:
                    break;

                case OP_CLOSURE: {
                    write_indent();
                    out << reg(a) << " = ";
                    if (bx < (int)f->protos.size()) {
                        Decompiler sub(&f->protos[bx]);
                        sub.indent = indent;
                        sub.decompile();
                        out << sub.out.str() << "\n";
                        set_reg(a, "function");
                    } else {
                        out << "function() end\n";
                        set_reg(a, "function() end");
                    }
                    break;
                }

                case OP_VARARG: {
                    write_indent();
                    if (b == 0) {
                        out << reg(a) << " = ...\n";
                        set_reg(a, "...");
                    } else {
                        for (int r = a; r < a + b - 1; r++) {
                            if (r > a) out << ", ";
                            out << reg(r);
                            set_reg(r, "...");
                        }
                        out << " = ...\n";
                    }
                    break;
                }

                default:
                    break;
            }
        }
    }
};

std::string decompile(const Function& f) {
    Decompiler dec(&f);
    dec.decompile();
    return dec.out.str();
}

} // namespace lua51

std::string decompile_lua51_bytecode(const uint8_t* data, size_t size) {
    lua51::BytecodeReader reader(data, size);

    if (!reader.read_header()) {
        return "";
    }

    try {
        lua51::Function f = reader.read_function();
        return lua51::decompile(f);
    } catch (...) {
        return "";
    }
}

extern std::vector<std::string> scan_luas_recursive(const std::string& root);

static std::string determine_bank_name(const std::string& path) {
    std::vector<std::string> banks = {
        "gamescripts\\scripts\\",
        "gamescripts_r\\scripts\\",
        "guiscripts\\scripts\\",
        "guiscripts\\art\\"
    };
    std::string path_lower = path;
    std::transform(path_lower.begin(), path_lower.end(), path_lower.begin(), ::tolower);

    for (const auto& bank : banks) {
        std::string bank_lower = bank;
        std::transform(bank_lower.begin(), bank_lower.end(), bank_lower.begin(), ::tolower);
        if (path_lower.find(bank_lower) != std::string::npos) {
            return bank;
        }
    }
    return "";
}

static std::string build_output_path(const std::string& input_path, const std::string& bank_name) {
    if (bank_name.empty()) {
        return "";
    }

    std::string path_lower = input_path;
    std::transform(path_lower.begin(), path_lower.end(), path_lower.begin(), ::tolower);
    std::string bank_lower = bank_name;
    std::transform(bank_lower.begin(), bank_lower.end(), bank_lower.begin(), ::tolower);

    size_t pos = path_lower.find(bank_lower);
    if (pos == std::string::npos) {
        return "";
    }

    std::string before = input_path.substr(0, pos + bank_name.length());
    std::string after = input_path.substr(pos + bank_name.length());

#ifdef _WIN32
    std::string result = before + "decompiled\\" + after;
    size_t double_sep;
    while ((double_sep = result.find("\\\\")) != std::string::npos) {
        result.replace(double_sep, 2, "\\");
    }
#else
    std::string result = before + "decompiled/" + after;
    size_t double_sep;
    while ((double_sep = result.find("//")) != std::string::npos) {
        result.replace(double_sep, 2, "/");
    }
#endif

    return result;
}

static bool copy_file_simple(const std::string& src, const std::string& dst) {
    std::ifstream in(src, std::ios::binary);
    if (!in) return false;

    std::ofstream out(dst, std::ios::binary);
    if (!out) return false;

    out << in.rdbuf();
    return true;
}

std::string read_lua_file_content(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return "-- Error: Could not open file";
    }

    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);

    if (size > 10 * 1024 * 1024) {
        return "-- Error: File too large to preview (>10MB)";
    }

    std::vector<uint8_t> buffer(size);
    file.read(reinterpret_cast<char*>(buffer.data()), size);
    file.close();

    bool is_bytecode = false;
    if (size >= 4) {
        if (buffer[0] == 0x1B && buffer[1] == 'L' && buffer[2] == 'u' && buffer[3] == 'a') {
            is_bytecode = true;
        }
    }

    if (!is_bytecode) {
        return std::string(buffer.begin(), buffer.end());
    }

    return decompile_lua51_bytecode(buffer.data(), buffer.size());
}

void dump_single_lua_file(const std::string& path, const std::string& root_dir) {
    std::string bank_name = determine_bank_name(path);
    std::string out_path = build_output_path(path, bank_name);

    if (out_path.empty() || path == out_path) {
        std::filesystem::path rel = std::filesystem::relative(path, root_dir);
        out_path = (std::filesystem::path(root_dir) / "extracted" / "lua" / rel).string();
    }

    std::filesystem::path out_p(out_path);
    std::error_code ec;
    std::filesystem::create_directories(out_p.parent_path(), ec);

    std::string content = read_lua_file_content(path);

    std::ofstream out(out_path);
    if (out.is_open()) {
        out << content;
        out.close();
        show_completion_box("Dumped to:\n" + out_path);
    } else {
        show_error_box("Failed to write:\n" + out_path);
    }
}

void dump_all_lua_files(const std::string& root_dir) {
    auto lua_paths = scan_luas_recursive(root_dir);

    if (lua_paths.empty()) {
        show_completion_box("No Lua files found.");
        return;
    }

    progress_open((int)lua_paths.size(), "Dumping Lua files...");

    std::thread([lua_paths, root_dir]() {
        int success_count = 0;
        int fail_count = 0;

        for (size_t i = 0; i < lua_paths.size(); ++i) {
            if (S.cancel_requested) break;

            std::filesystem::path p(lua_paths[i]);
            progress_update((int)i + 1, (int)lua_paths.size(), p.filename().string());

            std::string bank_name = determine_bank_name(lua_paths[i]);
            std::string out_path = build_output_path(lua_paths[i], bank_name);

            if (out_path.empty() || lua_paths[i] == out_path) {
                std::filesystem::path rel = std::filesystem::relative(lua_paths[i], root_dir);
                out_path = (std::filesystem::path(root_dir) / "extracted" / "lua" / rel).string();
            }

            std::filesystem::path out_p(out_path);
            std::error_code ec;
            std::filesystem::create_directories(out_p.parent_path(), ec);

            if (lua_paths[i] == out_path) {
                fail_count++;
                continue;
            }

            std::string content = read_lua_file_content(lua_paths[i]);

            std::ofstream out(out_path);
            if (out.is_open()) {
                out << content;
                out.close();
                success_count++;
            } else {
                fail_count++;
            }
        }

        progress_done();

        std::string msg = "Lua dump complete.\n";
        msg += "Processed: " + std::to_string(success_count);
        if (fail_count > 0) {
            msg += "\nFailed: " + std::to_string(fail_count);
        }

        show_completion_box(msg);
    }).detach();
}