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
        read_byte();
        return s;
    }

    bool read_header() {

        if (read_byte() != 0x1B) return false;
        if (read_byte() != 'L') return false;
        if (read_byte() != 'u') return false;
        if (read_byte() != 'a') return false;

        uint8_t version = read_byte();
        if (version != 0x51) return false;

        read_byte();

        little_endian = read_byte() == 1;

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

        int sizecode = read_int();
        f.code.resize(sizecode);
        for (int i = 0; i < sizecode; i++) {
            f.code[i] = read_int();
        }

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

        int sizep = read_int();
        f.protos.resize(sizep);
        for (int i = 0; i < sizep; i++) {
            f.protos[i] = read_function();
        }

        int sizelineinfo = read_int();
        f.lineinfo.resize(sizelineinfo);
        for (int i = 0; i < sizelineinfo; i++) {
            f.lineinfo[i] = read_int();
        }

        int sizelocvars = read_int();
        f.locals.resize(sizelocvars);
        for (int i = 0; i < sizelocvars; i++) {
            f.locals[i].name = read_string();
            f.locals[i].startpc = read_int();
            f.locals[i].endpc = read_int();
        }

        int sizeupvalues = read_int();
        f.upvalues.resize(sizeupvalues);
        for (int i = 0; i < sizeupvalues; i++) {
            f.upvalues[i].name = read_string();
        }

        return f;
    }
};
