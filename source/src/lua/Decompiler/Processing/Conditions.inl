static int compareBlocks(BlockPtr a, BlockPtr b) {
    if (a->begin < b->begin) return -1;
    if (a->begin > b->begin) return 1;
    if (a->end > b->end) return -1;
    if (a->end < b->end) return 1;
    if (a->isContainer() && !b->isContainer()) return -1;
    if (!a->isContainer() && b->isContainer()) return 1;
    return 0;
}

BranchPtr Decompiler::popCondition(Stack<BranchPtr>& stack) {
    BranchPtr branch = stack.pop();
    if (backup) backup->push(branch);
    if (std::dynamic_pointer_cast<TestSetNode>(branch)) throw std::runtime_error("popCondition TestSet");
    int beginX = branch->begin;
    if (beginX >= 1 && beginX <= length && code.op(beginX) == Op::JMP) {
        beginX += 1 + code.sBx(beginX);
    }
    while (!stack.empty()) {
        BranchPtr next = stack.peek();
        if (std::dynamic_pointer_cast<TestSetNode>(next)) break;
        if (next->end == beginX) {
            branch = std::make_shared<OrBranch>(popCondition(stack)->invert(), branch);
        } else if (next->end == branch->end) {
            branch = std::make_shared<AndBranch>(popCondition(stack), branch);
        } else {
            break;
        }
    }
    return branch;
}

BranchPtr Decompiler::popSetCondition(Stack<BranchPtr>& stack, int assignEnd, int target) {
    stack.push(std::make_shared<AssignNode>(assignEnd - 1, assignEnd, assignEnd));
    return helperPopSetCondition(stack, false, assignEnd, target);
}

BranchPtr Decompiler::popCompareSetCondition(Stack<BranchPtr>& stack, int assignEnd, int target) {
    BranchPtr top = stack.pop();
    bool invert = false;
    if (top->begin >= 1 && top->begin <= length && code.B(top->begin) == 0) invert = true;
    top->begin = assignEnd;
    top->end = assignEnd;
    stack.push(top);
    return helperPopSetCondition(stack, invert, assignEnd, target);
}

int Decompiler::adjustLine(int line, int target) {
    int testline = line;
    while (testline >= 1 && testline <= length && code.op(testline) == Op::LOADBOOL &&
           (target == -1 || code.A(testline) == target)) {
        testline--;
    }
    if (testline == line) return testline;
    testline++;
    if (testline <= length && code.C(testline) != 0) return testline + 2;
    return testline + 1;
}

BranchPtr Decompiler::helperPopSetCondition(Stack<BranchPtr>& stack, bool invert, int assignEnd, int target) {
    BranchPtr branch = stack.pop();
    int beginX = branch->begin;
    int endX = branch->end;
    if (invert) branch = branch->invert();
    beginX = adjustLine(beginX, target);
    endX = adjustLine(endX, target);
    int btarget = branch->setTarget;
    while (!stack.empty()) {
        BranchPtr next = stack.peek();
        bool ninvert;
        int nend = next->end;
        if (nend >= 1 && nend <= length && code.op(nend) == Op::LOADBOOL &&
            (target == -1 || code.A(nend) == target)) {
            ninvert = code.B(nend) != 0;
            nend = adjustLine(nend, target);
        } else if (auto ts = std::dynamic_pointer_cast<TestSetNode>(next)) {
            ninvert = ts->invertFlag;
        } else if (auto tn = std::dynamic_pointer_cast<TestNode>(next)) {
            ninvert = tn->invertFlag;
        } else {
            ninvert = false;
            if (nend >= assignEnd) break;
        }
        int addr = (ninvert == invert) ? endX : beginX;
        if (addr == nend) {
            if (ninvert) {
                branch = std::make_shared<OrBranch>(helperPopSetCondition(stack, ninvert, assignEnd, target), branch);
            } else {
                branch = std::make_shared<AndBranch>(helperPopSetCondition(stack, ninvert, assignEnd, target), branch);
            }
            branch->end = nend;
        } else {
            if (!std::dynamic_pointer_cast<TestSetNode>(branch)) {
                stack.push(branch);
                branch = popCondition(stack);
            }
            break;
        }
    }
    branch->isSet = true;
    branch->setTarget = btarget;
    return branch;
}

bool Decompiler::isStatement(int line, int testRegister) {
    switch (code.op(line)) {
        case Op::MOVE: case Op::LOADK: case Op::LOADBOOL: case Op::GETUPVAL:
        case Op::GETTABUP: case Op::GETGLOBAL: case Op::GETTABLE: case Op::NEWTABLE:
        case Op::NEWTABLE50: case Op::ADD: case Op::SUB: case Op::MUL: case Op::DIV:
        case Op::MOD: case Op::POW: case Op::UNM: case Op::NOT: case Op::LEN:
        case Op::IDIV: case Op::BAND: case Op::BOR: case Op::BXOR: case Op::SHL:
        case Op::SHR: case Op::BNOT: case Op::CONCAT: case Op::CLOSURE:
            return r->isLocal(code.A(line), line) || code.A(line) == testRegister;
        case Op::LOADNIL:
            for (int reg = code.A(line); reg <= code.B(line); reg++) {
                if (r->isLocal(reg, line)) return true;
            }
            return false;
        case Op::SETGLOBAL: case Op::SETUPVAL: case Op::SETTABUP: case Op::SETTABLE:
        case Op::JMP: case Op::TAILCALL: case Op::RETURN: case Op::FORLOOP:
        case Op::FORPREP: case Op::TFORPREP: case Op::TFORCALL: case Op::TFORLOOP:
        case Op::CLOSE:
            return true;
        case Op::SELF:
            return r->isLocal(code.A(line), line) || r->isLocal(code.A(line) + 1, line);
        case Op::EQ: case Op::LT: case Op::LE: case Op::TEST: case Op::TESTSET:
        case Op::TEST50: case Op::SETLIST: case Op::SETLISTO: case Op::SETLIST50:
            return false;
        case Op::CALL: {
            int a = code.A(line);
            int c = code.C(line);
            if (c == 1) return true;
            if (c == 0) c = regCount - a + 1;
            for (int reg = a; reg < a + c - 1; reg++) {
                if (r->isLocal(reg, line)) return true;
            }
            return (c == 2 && a == testRegister);
        }
        case Op::VARARG: {
            int a = code.A(line);
            int b = code.B(line);
            if (b == 0) b = regCount - a + 1;
            for (int reg = a; reg < a + b - 1; reg++) {
                if (r->isLocal(reg, line)) return true;
            }
            return false;
        }
        default: return false;
    }
}

int Decompiler::getAssignment(int line) {
    switch (code.op(line)) {
        case Op::MOVE: case Op::LOADK: case Op::LOADBOOL: case Op::GETUPVAL:
        case Op::GETTABUP: case Op::GETGLOBAL: case Op::GETTABLE: case Op::NEWTABLE:
        case Op::NEWTABLE50: case Op::ADD: case Op::SUB: case Op::MUL: case Op::DIV:
        case Op::MOD: case Op::POW: case Op::UNM: case Op::NOT: case Op::LEN:
        case Op::IDIV: case Op::BAND: case Op::BOR: case Op::BXOR: case Op::SHL:
        case Op::SHR: case Op::BNOT: case Op::CONCAT: case Op::CLOSURE:
            return code.A(line);
        case Op::LOADNIL:
            if (code.A(line) == code.B(line)) return code.A(line);
            return -1;
        case Op::CALL:
            if (code.C(line) == 2) return code.A(line);
            return -1;
        case Op::VARARG:
            if (code.C(line) == 2) return code.B(line);
            return -1;
        default: return -1;
    }
}
