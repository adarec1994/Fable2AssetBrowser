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
