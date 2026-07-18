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
