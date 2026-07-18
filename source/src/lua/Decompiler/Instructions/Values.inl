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
