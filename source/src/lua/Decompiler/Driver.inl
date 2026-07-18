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
