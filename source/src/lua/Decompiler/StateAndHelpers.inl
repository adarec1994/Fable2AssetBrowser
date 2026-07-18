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
