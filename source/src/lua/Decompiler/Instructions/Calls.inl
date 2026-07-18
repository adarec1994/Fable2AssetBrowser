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

