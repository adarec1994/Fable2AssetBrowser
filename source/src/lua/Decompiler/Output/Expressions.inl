ExpressionPtr Function::getConstantExpression(int idx) {
    return std::make_shared<ConstantExpression>(getConstant(idx), idx);
}
ExpressionPtr Function::getGlobalExpression(int idx) {
    return std::make_shared<GlobalExpression>(getGlobalName(idx), idx);
}
ExpressionPtr Upvalues::getExpression(int idx) {
    return std::make_shared<UpvalueExpression>(getName(idx));
}
ExpressionPtr Registers::getExpression(int reg, int line) {
    if (isLocal(reg, line - 1)) {
        return std::make_shared<LocalVariable>(getDeclaration(reg, line - 1));
    }
    if (reg < 0 || reg >= regCount || line - 1 < 0 || line - 1 > length) {
        Constant c; return std::make_shared<ConstantExpression>(c, -1);
    }
    auto v = values[(size_t)reg][(size_t)(line - 1)];
    if (!v) {
        Constant c; return std::make_shared<ConstantExpression>(c, -1);
    }
    return v;
}
ExpressionPtr Registers::getKExpression(int reg, int line) {
    if (f.isConstant(reg)) return f.getConstantExpression(f.constantIndex(reg));
    return getExpression(reg, line);
}
TargetPtr Registers::getTarget(int reg, int line) {
    if (!isLocal(reg, line)) throw std::runtime_error("no decl");
    return std::make_shared<VariableTarget>(decls[(size_t)reg][(size_t)line]);
}

void Expression::printSequence(Decompiler& d, Output& out, const std::vector<ExpressionPtr>& exprs, bool linebreak, bool multiple) {
    int n = (int)exprs.size();
    int i = 1;
    for (auto& expr : exprs) {
        bool last = (i == n);
        if (expr->isMultiple()) last = true;
        if (last) {
            if (multiple) expr->printMultiple(d, out);
            else expr->print(d, out);
            break;
        } else {
            expr->print(d, out);
            out.print(",");
            if (linebreak) out.println();
            else out.print(" ");
        }
        i++;
    }
}

void Statement::printSequence(Decompiler& d, Output& out, std::vector<StatementPtr>& stmts) {
    int n = (int)stmts.size();
    for (int i = 0; i < n; i++) {
        bool last = (i + 1 == n);
        auto& stmt = stmts[(size_t)i];
        if (stmt->beginsWithParen() && i > 0) {
            out.print(";");
        }
        if (last) stmt->printTail(d, out);
        else stmt->print(d, out);
        if (!stmt->isIfThenElseBlock()) out.println();
    }
}

void Return::printTail(Decompiler& d, Output& out) {
    out.print("return");
    if (!values.empty()) {
        out.print(" ");
        Expression::printSequence(d, out, values, false, true);
    }
}

void Assignment::print(Decompiler& d, Output& out) {
    bool functionSugar = false;
    if (targets.size() == 1 && values.size() == 1 && values[0]->isClosure() &&
        targets[0]->isFunctionName()) {
        functionSugar = true;
        if (declareFlag && declareStart < values[0]->closureUpvalueLine()) {
            functionSugar = false;
        }
    }
    if (functionSugar) {
        values[0]->printClosure(d, out, targets[0]);
    } else {
        if (declareFlag) out.print("local ");
        targets[0]->print(d, out);
        for (size_t i = 1; i < targets.size(); i++) {
            out.print(", ");
            targets[i]->print(d, out);
        }
        if (!declareFlag || !allnil) {
            out.print(" = ");
            Expression::printSequence(d, out, values, false, false);
        }
        if (!comment.empty()) {
            out.print(" -- ");
            out.print(comment);
        }
    }
}

void FunctionCall::print(Decompiler& d, Output& out) {
    std::vector<ExpressionPtr> args;
    if (isMethodCall()) {
        ExpressionPtr obj = function->getTable();
        if (obj->isUngrouped()) { out.print("("); obj->print(d, out); out.print(")"); }
        else obj->print(d, out);
        out.print(":");
        out.print(function->getField());
        for (size_t i = 1; i < arguments.size(); i++) args.push_back(arguments[i]);
    } else {
        if (function->isUngrouped()) { out.print("("); function->print(d, out); out.print(")"); }
        else function->print(d, out);
        for (auto& a : arguments) args.push_back(a);
    }
    out.print("(");
    Expression::printSequence(d, out, args, false, true);
    out.print(")");
}

void TableLiteral::print(Decompiler& d, Output& out) {
    std::sort(entries.begin(), entries.end(), [](const TableLiteralEntry& a, const TableLiteralEntry& b) {
        return a.timestamp < b.timestamp;
    });
    listLength = 1;
    if (entries.empty()) { out.print("{}"); return; }
    bool lineBreak = (isList && entries.size() > 5) || (isObject && entries.size() > 2) || !isObject;
    if (!lineBreak) {
        for (auto& e : entries) if (!e.value->isBrief()) { lineBreak = true; break; }
    }
    out.print("{");
    if (lineBreak) { out.println(); out.indent(); }
    auto printEntry = [&](size_t index) {
        auto& e = entries[index];
        bool mult = (index + 1 >= entries.size()) || e.value->isMultiple();
        if (isList && e.key->isInteger() && listLength == e.key->asInteger()) {
            if (mult) e.value->printMultiple(d, out);
            else e.value->print(d, out);
            listLength++;
        } else if (isObject && e.key->isIdentifier()) {
            out.print(e.key->asName());
            out.print(" = ");
            e.value->print(d, out);
        } else {
            out.print("[");
            e.key->printBraced(d, out);
            out.print("] = ");
            e.value->print(d, out);
        }
    };
    printEntry(0);
    if (!entries[0].value->isMultiple()) {
        for (size_t i = 1; i < entries.size(); i++) {
            out.print(",");
            if (lineBreak) out.println();
            else out.print(" ");
            printEntry(i);
            if (entries[i].value->isMultiple()) break;
        }
    }
    if (lineBreak) { out.dedent(); out.println(); }
    out.print("}");
}

void ClosureExpression::print(Decompiler& outer, Output& out) {
    Decompiler sub(*function, *outer.code.ex, *outer.code.opcodes, &outer.declList, upvalueLine);
    sub.decompile();
    out.print("function");
    printMainHelper(out, sub, true);
}
void ClosureExpression::printClosure(Decompiler& outer, Output& out, TargetPtr name) {
    Decompiler sub(*function, *outer.code.ex, *outer.code.opcodes, &outer.declList, upvalueLine);
    sub.decompile();
    out.print("function ");
    bool useMethodSyntax = false;
    if (function->numParams >= 1 && !sub.declList.empty() && sub.declList[0]->name == "self") {
        auto tt = std::dynamic_pointer_cast<TableTarget>(name);
        if (tt) useMethodSyntax = true;
    }
    if (useMethodSyntax) {
        name->printMethod(outer, out);
        printMainHelper(out, sub, false);
    } else {
        name->print(outer, out);
        printMainHelper(out, sub, true);
    }
}
void ClosureExpression::printMainHelper(Output& out, Decompiler& sub, bool includeFirst) {
    out.print("(");
    int start = includeFirst ? 0 : 1;
    if (sub.params > start) {
        sub.declList[(size_t)start]->name;
        out.print(sub.declList[(size_t)start]->name);
        for (int i = start + 1; i < sub.params; i++) {
            out.print(", ");
            out.print(sub.declList[(size_t)i]->name);
        }
    }
    if ((sub.vararg & 1) == 1) {
        if (sub.params > start) out.print(", ...");
        else out.print("...");
    }
    out.print(")");
    out.println();
    out.indent();
    sub.print(out);
    out.dedent();
    out.print("end");
}

