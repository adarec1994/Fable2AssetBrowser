OperationPtr Block::process(Decompiler& d) {
    auto self = shared_from_this();
    StatementPtr stmtSelf = std::dynamic_pointer_cast<Statement>(self);
    class BlockOp : public Operation {
    public:
        StatementPtr stmt;
        BlockOp(int l, StatementPtr s) : Operation(l), stmt(s) {}
        StatementPtr process(Registers& r, BlockPtr b) override { return stmt; }
    };
    return std::make_shared<BlockOp>(end - 1, stmtSelf);
}

void OuterBlock::print(Decompiler& d, Output& out) {
    if (!statements.empty()) {
        auto last = statements.back();
        auto ret = std::dynamic_pointer_cast<Return>(last);
        if (ret) statements.pop_back();
    }
    Statement::printSequence(d, out, statements);
}

void IfThenEndBlock::print(Decompiler& d, Output& out) {
    out.print("if ");
    branch->asExpression(r)->print(d, out);
    out.print(" then");
    out.println();
    out.indent();
    Statement::printSequence(d, out, statements);
    out.dedent();
    out.print("end");
}

void IfThenElseBlock::print(Decompiler& d, Output& out) {
    out.print("if ");
    branch->asExpression(r)->print(d, out);
    out.print(" then");
    out.println();
    out.indent();
    if (statements.size() == 1) {
        auto br = std::dynamic_pointer_cast<Break>(statements[0]);
        if (br && br->target == loopback) {
            out.dedent();
            return;
        }
    }
    Statement::printSequence(d, out, statements);
    out.dedent();
    if (emptyElse) {
        out.println("else");
        out.print("end");
    }
}

void ElseEndBlock::print(Decompiler& d, Output& out) {
    if (statements.size() == 1) {
        auto inner = std::dynamic_pointer_cast<IfThenEndBlock>(statements[0]);
        if (inner) {
            out.print("else");
            inner->print(d, out);
            return;
        }
    }
    if (statements.size() == 2) {
        auto firstIf = std::dynamic_pointer_cast<IfThenElseBlock>(statements[0]);
        auto secondElse = std::dynamic_pointer_cast<ElseEndBlock>(statements[1]);
        if (firstIf && secondElse) {
            out.print("else");
            firstIf->print(d, out);
            secondElse->print(d, out);
            return;
        }
    }
    out.print("else");
    out.println();
    out.indent();
    Statement::printSequence(d, out, statements);
    out.dedent();
    out.print("end");
}

void DoEndBlock::print(Decompiler& d, Output& out) {
    out.println("do");
    out.indent();
    Statement::printSequence(d, out, statements);
    out.dedent();
    out.print("end");
}

void WhileBlock::print(Decompiler& d, Output& out) {
    out.print("while ");
    branch->asExpression(r)->print(d, out);
    out.print(" do");
    out.println();
    out.indent();
    Statement::printSequence(d, out, statements);
    out.dedent();
    out.print("end");
}

void RepeatBlock::print(Decompiler& d, Output& out) {
    out.print("repeat");
    out.println();
    out.indent();
    Statement::printSequence(d, out, statements);
    out.dedent();
    out.print("until ");
    branch->asExpression(r)->print(d, out);
}

void ForBlock::print(Decompiler& d, Output& out) {
    out.print("for ");
    if (r.isLocal(reg + 3, begin - 1)) {
        r.getTarget(reg + 3, begin - 1)->print(d, out);
    } else {
        out.print("i");
    }
    out.print(" = ");
    auto startE = r.getValue(reg, begin - 1);
    if (startE) startE->print(d, out); else out.print("nil");
    out.print(", ");
    auto stopE = r.getValue(reg + 1, begin - 1);
    if (stopE) stopE->print(d, out); else out.print("nil");
    auto step = r.getValue(reg + 2, begin - 1);
    if (step && (!step->isInteger() || step->asInteger() != 1)) {
        out.print(", ");
        step->print(d, out);
    }
    out.print(" do");
    out.println();
    out.indent();
    Statement::printSequence(d, out, statements);
    out.dedent();
    out.print("end");
}

void TForBlock::print(Decompiler& d, Output& out) {
    out.print("for ");
    if (r.isLocal(reg + 3, begin - 1)) {
        r.getTarget(reg + 3, begin - 1)->print(d, out);
    } else {
        out.print("k");
    }
    for (int r1 = reg + 4; r1 <= reg + 2 + len; r1++) {
        out.print(", ");
        if (r.isLocal(r1, begin - 1)) {
            r.getTarget(r1, begin - 1)->print(d, out);
        } else {
            out.print("v");
        }
    }
    out.print(" in ");
    auto value = r.getValue(reg, begin - 1);
    if (value) value->print(d, out); else out.print("nil");
    if (value && !value->isMultiple()) {
        out.print(", ");
        value = r.getValue(reg + 1, begin - 1);
        if (value) value->print(d, out); else out.print("nil");
        if (value && !value->isMultiple()) {
            out.print(", ");
            value = r.getValue(reg + 2, begin - 1);
            if (value) value->print(d, out); else out.print("nil");
        }
    }
    out.print(" do");
    out.println();
    out.indent();
    Statement::printSequence(d, out, statements);
    out.dedent();
    out.print("end");
}

void AlwaysLoop::print(Decompiler& d, Output& out) {
    out.println("while true do");
    out.indent();
    Statement::printSequence(d, out, statements);
    out.dedent();
    out.print("end");
}

