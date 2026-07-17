#include "lua_decompile.h"

#include <algorithm>
#include <functional>
#include <stdexcept>

namespace lua {

namespace {

struct ParseSizes {
    int int_size      = 4;
    int sizeT_size    = 4;
    int number_size   = 8;
    bool number_is_integral = false;
};

int32_t read_sized_int(ByteBuffer& buf, int n) {
    switch (n) {
        case 0: return 0;
        case 1: return (int8_t)buf.get();
        case 2: return buf.getShort();
        case 4: return buf.getInt();
        case 8: return (int32_t)buf.getLong();
        default: {
            int64_t v = 0;
            if (buf.order() == ByteBuffer::Endian::LE) {
                for (int i = 0; i < n; i++) v |= ((int64_t)buf.get()) << (8 * i);
            } else {
                for (int i = 0; i < n; i++) { v <<= 8; v |= (int64_t)buf.get(); }
            }
            return (int32_t)v;
        }
    }
}

LString read_string(ByteBuffer& buf, const ParseSizes& sz) {
    LString s;
    int32_t len = read_sized_int(buf, sz.sizeT_size);
    if (len == 0) { s.is_nil = true; return s; }
    s.value.resize((size_t)len);
    buf.getBytes(reinterpret_cast<uint8_t*>(&s.value[0]), (size_t)len);
    while (!s.value.empty() && s.value.back() == '\0') s.value.pop_back();
    return s;
}

double read_number(ByteBuffer& buf, const ParseSizes& sz) {
    if (sz.number_is_integral) {
        return (double)((sz.number_size == 4) ? buf.getInt() : buf.getLong());
    } else {
        return (sz.number_size == 4) ? (double)buf.getFloat() : buf.getDouble();
    }
}

LObject read_constant(ByteBuffer& buf, const ParseSizes& sz) {
    LObject o;
    uint8_t tag = buf.get();
    switch (tag) {
        case 0: o.tag = LObject::NIL;    return o;
        case 1: o.tag = LObject::BOOL;   o.b = (buf.get() != 0); return o;
        case 3: o.tag = LObject::NUMBER; o.n = read_number(buf, sz); return o;
        case 4: o.tag = LObject::STRING; o.s = read_string(buf, sz); return o;
        default:
            throw std::runtime_error("lua: unknown Lua 5.1 constant tag " + std::to_string((int)tag));
    }
}

LLocal read_local(ByteBuffer& buf, const ParseSizes& sz) {
    LLocal loc;
    LString name = read_string(buf, sz);
    loc.name  = name.deref();
    loc.start = read_sized_int(buf, sz.int_size);
    loc.end   = read_sized_int(buf, sz.int_size);
    return loc;
}

std::shared_ptr<LFunction> read_function(ByteBuffer& buf, const ParseSizes& sz);

std::shared_ptr<LFunction> read_function(ByteBuffer& buf, const ParseSizes& sz) {
    auto f = std::make_shared<LFunction>();

    LString src = read_string(buf, sz);
    f->sourceName       = src.deref();
    f->lineBegin        = read_sized_int(buf, sz.int_size);
    f->lineEnd          = read_sized_int(buf, sz.int_size);
    f->numUpvalues      = buf.get();
    f->numParams        = buf.get();
    f->vararg           = buf.get();
    f->maximumStackSize = buf.get();

    {
        int32_t n = read_sized_int(buf, sz.int_size);
        if (n < 0 || n > 1000000)
            throw std::runtime_error("lua: implausible code length " + std::to_string(n));
        f->code.resize((size_t)n);
        for (int32_t i = 0; i < n; i++) f->code[i] = (uint32_t)buf.getInt();
    }

    {
        int32_t n = read_sized_int(buf, sz.int_size);
        if (n < 0 || n > 1000000)
            throw std::runtime_error("lua: implausible constants count " + std::to_string(n));
        f->constants.reserve((size_t)n);
        for (int32_t i = 0; i < n; i++) f->constants.push_back(read_constant(buf, sz));
    }

    {
        int32_t n = read_sized_int(buf, sz.int_size);
        if (n < 0 || n > 100000)
            throw std::runtime_error("lua: implausible function-prototype count " + std::to_string(n));
        f->functions.reserve((size_t)n);
        for (int32_t i = 0; i < n; i++) {
            auto child = read_function(buf, sz);
            f->functions.push_back(child);
        }
    }

    f->upvalues.assign((size_t)f->numUpvalues, LUpvalue{});

    {
        int32_t n = read_sized_int(buf, sz.int_size);
        if (n < 0) throw std::runtime_error("lua: negative debug line count");
        f->lines.resize((size_t)n);
        for (int32_t i = 0; i < n; i++) f->lines[i] = read_sized_int(buf, sz.int_size);
    }
    {
        int32_t n = read_sized_int(buf, sz.int_size);
        if (n < 0) throw std::runtime_error("lua: negative locals count");
        f->locals.reserve((size_t)n);
        for (int32_t i = 0; i < n; i++) f->locals.push_back(read_local(buf, sz));
    }
    {
        int32_t n = read_sized_int(buf, sz.int_size);
        if (n < 0) throw std::runtime_error("lua: negative upvalue-names count");
        for (int32_t i = 0; i < n; i++) {
            LString name = read_string(buf, sz);
            if (i < (int32_t)f->upvalues.size()) f->upvalues[(size_t)i].name = name.deref();
        }
        if (n == 0 && f->lines.empty() && f->locals.empty()) f->stripped = true;
    }

    return f;
}

}

BHeader parse_lua_bytecode(const uint8_t* data, size_t size, const Configuration& cfg) {
    if (!data || size < 12)
        throw std::runtime_error("lua: bytecode buffer too small for a Lua header");

    static const uint8_t kSig[4] = { 0x1B, 0x4C, 0x75, 0x61 };
    if (std::memcmp(data, kSig, 4) != 0)
        throw std::runtime_error("lua: input does not have a Lua header signature");

    BHeader h;
    h.config = cfg;
    ByteBuffer buf(data, size, ByteBuffer::Endian::LE);
    buf.position(4);

    uint8_t versionNumber = buf.get();
    if (versionNumber != 0x51) {
        int major = versionNumber >> 4;
        int minor = versionNumber & 0x0F;
        throw std::runtime_error(
            "lua: chunk reports Lua version " + std::to_string(major) + "." +
            std::to_string(minor) + " - only Lua 5.1 is currently supported.");
    }
    h.lheader.version = versionNumber;

    uint8_t format          = buf.get();
    uint8_t endianness      = buf.get();
    uint8_t intSize         = buf.get();
    uint8_t sizeTSize       = buf.get();
    uint8_t instructionSize = buf.get();
    uint8_t numberSize      = buf.get();
    uint8_t numberIsInt     = buf.get();

    if (format != 0)
        throw std::runtime_error("lua: non-standard Lua format byte " + std::to_string(format));
    if (instructionSize != 4)
        throw std::runtime_error("lua: unsupported instruction size " + std::to_string(instructionSize));
    if (endianness > 1)
        throw std::runtime_error("lua: invalid endianness byte " + std::to_string(endianness));
    if (!(numberSize == 4 || numberSize == 8))
        throw std::runtime_error("lua: unsupported Lua number size " + std::to_string(numberSize));

    h.lheader.format           = format;
    h.lheader.endianness       = endianness;
    h.lheader.intSize          = intSize;
    h.lheader.sizeTSize        = sizeTSize;
    h.lheader.instructionSize  = instructionSize;
    h.lheader.numberSize       = numberSize;
    h.lheader.numberIsInt      = numberIsInt;

    h.endian = (endianness == 1) ? ByteBuffer::Endian::LE : ByteBuffer::Endian::BE;
    buf.order(h.endian);

    ParseSizes sz;
    sz.int_size              = intSize;
    sz.sizeT_size            = sizeTSize;
    sz.number_size           = numberSize;
    sz.number_is_integral    = (numberIsInt != 0);

    h.main = read_function(buf, sz);

    std::function<void(LFunction*)> set_parents = [&](LFunction* f) {
        for (auto& child : f->functions) {
            child->parent = f;
            set_parents(child.get());
        }
    };
    set_parents(h.main.get());

    h.extractor = std::make_shared<CodeExtract>();
    h.opcodes   = std::make_shared<OpcodeMap>(0x51);

    return h;
}

}
