std::vector<OperationPtr> Decompiler::processLine(int line) {
    std::vector<OperationPtr> ops;
    int A = code.A(line);
    int B = code.B(line);
    int C = code.C(line);
    int Bx = code.Bx(line);
    Op op = code.op(line);
    switch (op) {
        case Op::MOVE:
            ops.push_back(std::make_shared<RegisterSet>(line, A, r->getExpression(B, line)));
            break;
        case Op::LOADK:
            ops.push_back(std::make_shared<RegisterSet>(line, A, f.getConstantExpression(Bx)));
            break;
        case Op::LOADBOOL: {
            Constant c; c.type = 1; c.b = (B != 0);
            ops.push_back(std::make_shared<RegisterSet>(line, A, std::make_shared<ConstantExpression>(c, -1)));
            break;
        }
        case Op::LOADNIL: {
            int maximum = B;
            if (maximum < A) maximum = A;
            int a = A;
            while (a <= maximum) {
                Constant c;
                ops.push_back(std::make_shared<RegisterSet>(line, a, std::make_shared<ConstantExpression>(c, -1)));
                a++;
            }
            break;
        }
        case Op::GETUPVAL:
            ops.push_back(std::make_shared<RegisterSet>(line, A, upvalues->getExpression(B)));
            break;
        case Op::GETTABUP:
            ops.push_back(std::make_shared<RegisterSet>(line, A,
                std::make_shared<TableReference>(upvalues->getExpression(B), r->getKExpression(C, line))));
            break;
        case Op::GETGLOBAL:
            ops.push_back(std::make_shared<RegisterSet>(line, A, f.getGlobalExpression(Bx)));
            break;
        case Op::GETTABLE:
            ops.push_back(std::make_shared<RegisterSet>(line, A,
                std::make_shared<TableReference>(r->getExpression(B, line), r->getKExpression(C, line))));
            break;
        case Op::SETUPVAL:
            ops.push_back(std::make_shared<UpvalueSet>(line, upvalues->getName(B), r->getExpression(A, line)));
            break;
        case Op::SETTABUP:
            ops.push_back(std::make_shared<TableSet>(line,
                upvalues->getExpression(A),
                r->getKExpression(B, line),
                r->getKExpression(C, line), true, line));
            break;
        case Op::SETGLOBAL:
            ops.push_back(std::make_shared<GlobalSet>(line, f.getGlobalName(Bx), r->getExpression(A, line)));
            break;
        case Op::SETTABLE:
            ops.push_back(std::make_shared<TableSet>(line,
                r->getExpression(A, line),
                r->getKExpression(B, line),
                r->getKExpression(C, line), true, line));
            break;
        case Op::NEWTABLE:
            ops.push_back(std::make_shared<RegisterSet>(line, A,
                std::make_shared<TableLiteral>(fb2int(B), fb2int(C))));
            break;
        case Op::NEWTABLE50:
            ops.push_back(std::make_shared<RegisterSet>(line, A,
                std::make_shared<TableLiteral>(B, 1 << C)));
            break;
        case Op::SELF: {
            ExpressionPtr common = r->getExpression(B, line);
            ops.push_back(std::make_shared<RegisterSet>(line, A + 1, common));
            ops.push_back(std::make_shared<RegisterSet>(line, A,
                std::make_shared<TableReference>(common, r->getKExpression(C, line))));
            break;
        }
        case Op::ADD: case Op::SUB: case Op::MUL: case Op::DIV:
        case Op::MOD: case Op::POW: case Op::IDIV:
        case Op::BAND: case Op::BOR: case Op::BXOR:
        case Op::SHL: case Op::SHR: {
            std::string opStr;
            int prec, assoc = Expression::ASSOC_LEFT;
            switch (op) {
                case Op::ADD: opStr = "+"; prec = Expression::PREC_ADD; break;
                case Op::SUB: opStr = "-"; prec = Expression::PREC_ADD; break;
                case Op::MUL: opStr = "*"; prec = Expression::PREC_MUL; break;
                case Op::DIV: opStr = "/"; prec = Expression::PREC_MUL; break;
                case Op::MOD: opStr = "%"; prec = Expression::PREC_MUL; break;
                case Op::POW: opStr = "^"; prec = Expression::PREC_POW; assoc = Expression::ASSOC_RIGHT; break;
                case Op::IDIV: opStr = "//"; prec = Expression::PREC_MUL; break;
                case Op::BAND: opStr = "&"; prec = Expression::PREC_COMPARE; break;
                case Op::BOR: opStr = "|"; prec = Expression::PREC_COMPARE; break;
                case Op::BXOR: opStr = "~"; prec = Expression::PREC_COMPARE; break;
                case Op::SHL: opStr = "<<"; prec = Expression::PREC_COMPARE; break;
                case Op::SHR: opStr = ">>"; prec = Expression::PREC_COMPARE; break;
                default: opStr = "?"; prec = Expression::PREC_ADD; break;
            }
            ops.push_back(std::make_shared<RegisterSet>(line, A,
                std::make_shared<BinaryExpression>(opStr,
                    r->getKExpression(B, line), r->getKExpression(C, line), prec, assoc)));
            break;
        }
        case Op::UNM:
            ops.push_back(std::make_shared<RegisterSet>(line, A,
                std::make_shared<UnaryExpression>("-", r->getExpression(B, line), Expression::PREC_UNARY)));
            break;
        case Op::NOT:
            ops.push_back(std::make_shared<RegisterSet>(line, A,
                std::make_shared<UnaryExpression>("not ", r->getExpression(B, line), Expression::PREC_UNARY)));
            break;
        case Op::LEN:
            ops.push_back(std::make_shared<RegisterSet>(line, A,
                std::make_shared<UnaryExpression>("#", r->getExpression(B, line), Expression::PREC_UNARY)));
            break;
        case Op::BNOT:
            ops.push_back(std::make_shared<RegisterSet>(line, A,
                std::make_shared<UnaryExpression>("~", r->getExpression(B, line), Expression::PREC_UNARY)));
            break;
        case Op::CONCAT: {
            ExpressionPtr value = r->getExpression(C, line);
            int c = C;
            while (c-- > B) {
                value = std::make_shared<BinaryExpression>("..",
                    r->getExpression(c, line), value,
                    Expression::PREC_CONCAT, Expression::ASSOC_RIGHT);
            }
            ops.push_back(std::make_shared<RegisterSet>(line, A, value));
            break;
        }
        case Op::JMP:
        case Op::EQ: case Op::LT: case Op::LE:
        case Op::TEST: case Op::TESTSET: case Op::TEST50:
            break;
        case Op::CALL: {
            bool multiple = (C >= 3 || C == 0);
            int b = B;
            int c = C;
            if (b == 0) b = regCount - A;
            if (c == 0) c = regCount - A + 1;
            ExpressionPtr fn = r->getExpression(A, line);
            std::vector<ExpressionPtr> args;
            for (int reg = A + 1; reg <= A + b - 1; reg++) {
                args.push_back(r->getExpression(reg, line));
            }
            auto call = std::make_shared<FunctionCall>(fn, std::move(args), multiple);
            if (c == 1) {
                ops.push_back(std::make_shared<CallOperation>(line, call));
            } else if (c == 2 && !multiple) {
                ops.push_back(std::make_shared<RegisterSet>(line, A, call));
            } else {
                for (int reg = A; reg <= A + c - 2; reg++) {
                    ops.push_back(std::make_shared<RegisterSet>(line, reg, call));
                }
            }
            break;
        }
        case Op::TAILCALL: {
            int b = B;
            if (b == 0) b = regCount - A;
            ExpressionPtr fn = r->getExpression(A, line);
            std::vector<ExpressionPtr> args;
            for (int reg = A + 1; reg <= A + b - 1; reg++) {
                args.push_back(r->getExpression(reg, line));
            }
            auto call = std::make_shared<FunctionCall>(fn, std::move(args), true);
            ops.push_back(std::make_shared<ReturnOperation>(line, call));
            skip[(size_t)(line + 1)] = true;
            break;
        }
        case Op::RETURN: {
            int b = B;
            if (b == 0) b = regCount - A + 1;
            std::vector<ExpressionPtr> values;
            for (int reg = A; reg <= A + b - 2; reg++) {
                values.push_back(r->getExpression(reg, line));
            }
            ops.push_back(std::make_shared<ReturnOperation>(line, std::move(values)));
            break;
        }
        case Op::FORLOOP:
        case Op::FORPREP:
        case Op::TFORPREP:
        case Op::TFORCALL:
        case Op::TFORLOOP:
            break;
        case Op::SETLIST: {
            int c = C;
            if (c == 0) {
                if (line + 1 <= length) {
                    c = (int)code.codepoint(line + 1);
                    if (line + 1 < (int)skip.size()) skip[(size_t)(line + 1)] = true;
                } else c = 1;
            }
            int b = B;
            if (b == 0) b = regCount - A - 1;
            if (b < 0) b = 0;
            ExpressionPtr table = r->getValue(A, line);
            if (!table) break;
            for (int i = 1; i <= b; i++) {
                ops.push_back(std::make_shared<TableSet>(line, table,
                    std::make_shared<ConstantExpression>(Constant::fromInt((c - 1) * 50 + i), -1),
                    r->getExpression(A + i, line), false, r->getUpdated(A + i, line)));
            }
            break;
        }
        case Op::SETLIST50:
        case Op::SETLISTO: {
            int n = Bx % 32;
            ExpressionPtr table = r->getValue(A, line);
            if (!table) break;
            for (int i = 1; i <= n + 1; i++) {
                ops.push_back(std::make_shared<TableSet>(line, table,
                    std::make_shared<ConstantExpression>(Constant::fromInt(Bx - n + i), -1),
                    r->getExpression(A + i, line), false, r->getUpdated(A + i, line)));
            }
            break;
        }
        case Op::CLOSE:
            break;
        case Op::CLOSURE: {
            if (Bx < 0 || Bx >= (int)function.functions.size()) break;
            auto subF = function.functions[(size_t)Bx];
            if (!subF) break;
            ops.push_back(std::make_shared<RegisterSet>(line, A,
                std::make_shared<ClosureExpression>(subF, declList, line + 1)));
            for (int i = 0; i < subF->numUpvalues; i++) {
                int sl = line + 1 + i;
                if (sl >= 0 && sl < (int)skip.size()) skip[(size_t)sl] = true;
            }
            break;
        }
        case Op::VARARG: {
            bool multiple = (B != 2);
            int b = B;
            if (b == 0) b = regCount - A + 1;
            ExpressionPtr value = std::make_shared<Vararg>(b - 1, multiple);
            for (int reg = A; reg <= A + b - 2; reg++) {
                ops.push_back(std::make_shared<RegisterSet>(line, reg, value));
            }
            break;
        }
        default:
            break;
    }
    return ops;
}

bool Decompiler::isMoveIntoTarget(int line) {
    switch (code.op(line)) {
        case Op::MOVE:
            return r->isAssignable(code.A(line), line) && !r->isLocal(code.B(line), line);
        case Op::SETUPVAL:
        case Op::SETGLOBAL:
            return !r->isLocal(code.A(line), line);
        case Op::SETTABLE:
        case Op::SETTABUP: {
            int C = code.C(line);
            if (f.isConstant(C)) return false;
            return !r->isLocal(C, line);
        }
        default: return false;
    }
}

TargetPtr Decompiler::getMoveIntoTargetTarget(int line, int previous) {
    switch (code.op(line)) {
        case Op::MOVE: return r->getTarget(code.A(line), line);
        case Op::SETUPVAL: return std::make_shared<UpvalueTarget>(upvalues->getName(code.B(line)));
        case Op::SETGLOBAL: return std::make_shared<GlobalTarget>(f.getGlobalName(code.Bx(line)));
        case Op::SETTABLE: return std::make_shared<TableTarget>(
            r->getExpression(code.A(line), previous), r->getKExpression(code.B(line), previous));
        case Op::SETTABUP: return std::make_shared<TableTarget>(
            upvalues->getExpression(code.A(line)), r->getKExpression(code.B(line), previous));
        default: throw std::runtime_error("getMoveIntoTargetTarget");
    }
}

ExpressionPtr Decompiler::getMoveIntoTargetValue(int line, int previous) {
    int A = code.A(line), B = code.B(line), C = code.C(line);
    switch (code.op(line)) {
        case Op::MOVE: return r->getValue(B, previous);
        case Op::SETUPVAL:
        case Op::SETGLOBAL: return r->getExpression(A, previous);
        case Op::SETTABLE:
        case Op::SETTABUP:
            if (f.isConstant(C)) throw std::runtime_error("");
            return r->getExpression(C, previous);
        default: throw std::runtime_error("getMoveIntoTargetValue");
    }
}

std::shared_ptr<Assignment> Decompiler::processOperation(OperationPtr op, int line, int nextLine, BlockPtr block) {
    std::shared_ptr<Assignment> assign;
    bool wasMultiple = false;
    StatementPtr stmt = op->process(*r, block);
    if (stmt) {
        assign = std::dynamic_pointer_cast<Assignment>(stmt);
        if (assign) {
            if (!assign->getFirstValue() || !assign->getFirstValue()->isMultiple()) {
                block->addStatement(stmt);
            } else {
                wasMultiple = true;
            }
        } else {
            block->addStatement(stmt);
        }
        if (assign) {
            while (nextLine < block->end && isMoveIntoTarget(nextLine)) {
                TargetPtr tgt = getMoveIntoTargetTarget(nextLine, line + 1);
                ExpressionPtr val = getMoveIntoTargetValue(nextLine, line + 1);
                assign->addFirst(tgt, val);
                skip[(size_t)nextLine] = true;
                nextLine++;
            }
            if (wasMultiple && !assign->getFirstValue()->isMultiple()) {
                block->addStatement(stmt);
            }
        }
    }
    return assign;
}

