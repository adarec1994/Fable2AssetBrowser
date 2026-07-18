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

