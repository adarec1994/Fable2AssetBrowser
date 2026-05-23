#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace lua {

struct Configuration {
    bool rawstring = false;
};

class ByteBuffer {
public:
    enum class Endian { LE, BE };

    ByteBuffer(const uint8_t* data, size_t len, Endian e = Endian::LE)
        : data_(data), len_(len), pos_(0), endian_(e) {}

    void order(Endian e) { endian_ = e; }
    Endian order() const { return endian_; }

    size_t position() const { return pos_; }
    size_t size() const { return len_; }
    void position(size_t p) { pos_ = p; }
    void rewind() { pos_ = 0; }
    bool eof() const { return pos_ >= len_; }
    size_t remaining() const { return pos_ < len_ ? len_ - pos_ : 0; }

    uint8_t get() {
        if (pos_ >= len_) throw std::runtime_error("ByteBuffer: read past end (u8)");
        return data_[pos_++];
    }
    int16_t getShort() {
        ensure_(2);
        int16_t v = (endian_ == Endian::LE)
            ? (int16_t)(((uint16_t)data_[pos_]) | ((uint16_t)data_[pos_ + 1] << 8))
            : (int16_t)(((uint16_t)data_[pos_] << 8) | (uint16_t)data_[pos_ + 1]);
        pos_ += 2;
        return v;
    }
    int32_t getInt() {
        ensure_(4);
        uint32_t v = (endian_ == Endian::LE)
            ? ((uint32_t)data_[pos_])
              | ((uint32_t)data_[pos_ + 1] << 8)
              | ((uint32_t)data_[pos_ + 2] << 16)
              | ((uint32_t)data_[pos_ + 3] << 24)
            : ((uint32_t)data_[pos_] << 24)
              | ((uint32_t)data_[pos_ + 1] << 16)
              | ((uint32_t)data_[pos_ + 2] << 8)
              | ((uint32_t)data_[pos_ + 3]);
        pos_ += 4;
        return (int32_t)v;
    }
    int64_t getLong() {
        ensure_(8);
        uint64_t v;
        if (endian_ == Endian::LE) {
            v =  (uint64_t)data_[pos_]
              | ((uint64_t)data_[pos_ + 1] << 8)
              | ((uint64_t)data_[pos_ + 2] << 16)
              | ((uint64_t)data_[pos_ + 3] << 24)
              | ((uint64_t)data_[pos_ + 4] << 32)
              | ((uint64_t)data_[pos_ + 5] << 40)
              | ((uint64_t)data_[pos_ + 6] << 48)
              | ((uint64_t)data_[pos_ + 7] << 56);
        } else {
            v =  ((uint64_t)data_[pos_] << 56)
              | ((uint64_t)data_[pos_ + 1] << 48)
              | ((uint64_t)data_[pos_ + 2] << 40)
              | ((uint64_t)data_[pos_ + 3] << 32)
              | ((uint64_t)data_[pos_ + 4] << 24)
              | ((uint64_t)data_[pos_ + 5] << 16)
              | ((uint64_t)data_[pos_ + 6] << 8)
              |  (uint64_t)data_[pos_ + 7];
        }
        pos_ += 8;
        return (int64_t)v;
    }
    float getFloat() {
        uint32_t bits = (uint32_t)getInt();
        float f; std::memcpy(&f, &bits, sizeof(f)); return f;
    }
    double getDouble() {
        uint64_t bits = (uint64_t)getLong();
        double d; std::memcpy(&d, &bits, sizeof(d)); return d;
    }

    void getBytes(uint8_t* dst, size_t n) {
        ensure_(n);
        std::memcpy(dst, data_ + pos_, n);
        pos_ += n;
    }

private:
    void ensure_(size_t n) {
        if (pos_ + n > len_) throw std::runtime_error("ByteBuffer: read past end");
    }
    const uint8_t* data_;
    size_t len_;
    size_t pos_;
    Endian endian_;
};

struct LFunction;
struct LHeader;
struct BHeader;
class  CodeExtract;
class  OpcodeMap;

struct BInteger {
    int32_t value = 0;
    BInteger() = default;
    explicit BInteger(int32_t v) : value(v) {}
    int32_t asInt() const { return value; }
};

struct LString {
    std::string value;
    bool        is_nil = false;
    std::string deref() const { return value; }
};

struct LObject {
    enum Tag { NIL, BOOL, NUMBER, STRING } tag = NIL;
    bool        b = false;
    double      n = 0.0;
    LString     s;

    bool isNil()    const { return tag == NIL; }
    bool isBool()   const { return tag == BOOL; }
    bool isNumber() const { return tag == NUMBER; }
    bool isString() const { return tag == STRING; }
};

struct LLocal {
    std::string name;
    int32_t     start = 0;
    int32_t     end   = 0;
};

struct LUpvalue {
    std::string name;
};

struct LHeader {
    uint8_t  version = 0x51;
    uint8_t  format = 0;
    uint8_t  endianness = 1;
    uint8_t  intSize = 4;
    uint8_t  sizeTSize = 4;
    uint8_t  instructionSize = 4;
    uint8_t  numberSize = 8;
    uint8_t  numberIsInt = 0;
};

struct LFunction {
    BHeader*               header = nullptr;
    LFunction*             parent = nullptr;
    std::string            sourceName;
    int32_t                lineBegin = 0;
    int32_t                lineEnd   = 0;
    int                    numUpvalues = 0;
    int                    numParams   = 0;
    int                    vararg      = 0;
    int                    maximumStackSize = 0;
    std::vector<uint32_t>  code;
    std::vector<LObject>   constants;
    std::vector<std::shared_ptr<LFunction>> functions;
    std::vector<int32_t>   lines;
    std::vector<LLocal>    locals;
    std::vector<LUpvalue>  upvalues;
    bool                   stripped = false;
};

struct BHeader {
    Configuration                config;
    LHeader                      lheader;
    ByteBuffer::Endian           endian = ByteBuffer::Endian::LE;
    std::shared_ptr<LFunction>   main;
    std::shared_ptr<CodeExtract> extractor;
    std::shared_ptr<OpcodeMap>   opcodes;
};

BHeader parse_lua_bytecode(const uint8_t* data, size_t size,
                           const Configuration& cfg = {});

enum class OpcodeFormat { A, A_B, A_C, A_B_C, A_Bx, A_sBx, Ax, sBx, NONE };

enum class Op {
    MOVE, LOADK, LOADBOOL, LOADNIL,
    GETUPVAL, GETGLOBAL, GETTABLE,
    SETGLOBAL, SETUPVAL, SETTABLE,
    NEWTABLE, SELF,
    ADD, SUB, MUL, DIV, MOD, POW,
    UNM, NOT, LEN, CONCAT,
    JMP, EQ, LT, LE, TEST, TESTSET,
    CALL, TAILCALL, RETURN,
    FORLOOP, FORPREP, TFORLOOP,
    SETLIST, CLOSE, CLOSURE, VARARG,
    LOADKX, GETTABUP, SETTABUP, TFORCALL, EXTRAARG,
    NEWTABLE50, SETLIST50, SETLISTO, TFORPREP, TEST50,
    IDIV, BAND, BOR, BXOR, SHL, SHR, BNOT,
    UNKNOWN
};

const char*   op_name(Op op);
OpcodeFormat  op_format(Op op);

class CodeExtract {
public:
    int extract_op(uint32_t cp) const  { return  cp        & 0x3F; }
    int extract_A(uint32_t cp) const   { return (cp >>  6) & 0xFF; }
    int extract_C(uint32_t cp) const   { return (cp >> 14) & 0x1FF; }
    int extract_B(uint32_t cp) const   { return (cp >> 23) & 0x1FF; }
    int extract_Bx(uint32_t cp) const  { return (cp >> 14) & 0x3FFFF; }
    int extract_sBx(uint32_t cp) const { return extract_Bx(cp) - 131071; }
};

class OpcodeMap {
public:
    explicit OpcodeMap(int versionNumber);
    Op get(int n) const;
private:
    std::vector<Op> map_;
};

std::string codepoint_to_string(Op op, uint32_t cp, const CodeExtract& ex);

std::string decompile_lua_bytecode(const uint8_t* data, size_t size);

std::string disassemble(const LFunction& main, const CodeExtract& ex,
                        const OpcodeMap& opcodes);

}
