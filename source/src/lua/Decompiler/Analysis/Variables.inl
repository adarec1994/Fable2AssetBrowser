class CodeView {
public:
    const LFunction* f;
    const CodeExtract* ex;
    const OpcodeMap* opcodes;
    Op op(int line) const { return opcodes->get(ex->extract_op(f->code[(size_t)(line - 1)])); }
    int A(int line) const { return ex->extract_A(f->code[(size_t)(line - 1)]); }
    int B(int line) const { return ex->extract_B(f->code[(size_t)(line - 1)]); }
    int C(int line) const { return ex->extract_C(f->code[(size_t)(line - 1)]); }
    int Bx(int line) const { return ex->extract_Bx(f->code[(size_t)(line - 1)]); }
    int sBx(int line) const { return ex->extract_sBx(f->code[(size_t)(line - 1)]); }
    uint32_t codepoint(int line) const { return f->code[(size_t)(line - 1)]; }
    int length() const { return (int)f->code.size(); }
};

static bool vfIsConstRef(int rk) { return (rk & 0x100) != 0; }

struct VFState {
    bool temporary = false;
    bool local = false;
    bool read = false;
    bool written = false;
};

class VFStates {
public:
    VFStates(int regs, int lines)
        : registers_(regs), lines_(lines),
          data_((size_t)lines, std::vector<VFState>((size_t)regs)) {}
    VFState& get(int reg, int line) {
        if (reg < 0) reg = 0;
        if (reg >= registers_) reg = registers_ - 1;
        if (line < 1) line = 1;
        if (line > lines_) line = lines_;
        return data_[(size_t)(line - 1)][(size_t)reg];
    }
    void setLocal(int reg, int line) {
        if (reg < 0) return;
        if (reg >= registers_) reg = registers_ - 1;
        for (int r = 0; r <= reg; r++) get(r, line).local = true;
    }
    void setTemporary(int reg, int line) {
        if (reg < 0) reg = 0;
        for (int r = reg; r < registers_; r++) get(r, line).temporary = true;
    }
    int registers() const { return registers_; }
private:
    int registers_;
    int lines_;
    std::vector<std::vector<VFState>> data_;
};

static int g_vfCounter = 0;

static std::vector<DeclarationPtr> findVariables(const LFunction& f, const CodeExtract& ex,
                                                 const OpcodeMap& opcodes, int regCount, int numParams)
{
    int length = (int)f.code.size();
    std::vector<DeclarationPtr> result;
    if (length == 0) {
        for (int i = 0; i < numParams; i++) {
            auto d = std::make_shared<Declaration>();
            d->name = "arg" + std::to_string(i);
            d->begin = 0;
            d->end = 0;
            d->reg = i;
            result.push_back(d);
        }
        return result;
    }
    VFStates states(regCount, length);
    std::vector<bool> skip((size_t)length + 2, false);

    auto opAt = [&](int line) -> Op {
        return opcodes.get(ex.extract_op(f.code[(size_t)(line - 1)]));
    };
    auto A_ = [&](int line) -> int { return ex.extract_A(f.code[(size_t)(line - 1)]); };
    auto B_ = [&](int line) -> int { return ex.extract_B(f.code[(size_t)(line - 1)]); };
    auto C_ = [&](int line) -> int { return ex.extract_C(f.code[(size_t)(line - 1)]); };
    auto Bx_ = [&](int line) -> int { return ex.extract_Bx(f.code[(size_t)(line - 1)]); };

    for (int line = 1; line <= length; line++) {
        if (skip[(size_t)line]) continue;
        Op op = opAt(line);
        int A = A_(line), B = B_(line), C = C_(line);
        switch (op) {
            case Op::MOVE:
                states.get(A, line).written = true;
                states.get(B, line).read = true;
                states.setLocal(std::min(A, B), line);
                break;
            case Op::LOADK:
            case Op::LOADBOOL:
            case Op::GETUPVAL:
            case Op::GETGLOBAL:
            case Op::NEWTABLE:
            case Op::NEWTABLE50:
                states.get(A, line).written = true;
                break;
            case Op::LOADNIL: {
                int to = B;
                if (to < A) to = A;
                for (int r = A; r <= to && r < regCount; r++) states.get(r, line).written = true;
                break;
            }
            case Op::GETTABLE:
            case Op::GETTABUP:
                states.get(A, line).written = true;
                if (!vfIsConstRef(B)) states.get(B, line).read = true;
                if (!vfIsConstRef(C)) states.get(C, line).read = true;
                break;
            case Op::SETGLOBAL:
            case Op::SETUPVAL:
                states.get(A, line).read = true;
                break;
            case Op::SETTABLE:
            case Op::SETTABUP:
                states.get(A, line).read = true;
                if (!vfIsConstRef(B)) states.get(B, line).read = true;
                if (!vfIsConstRef(C)) states.get(C, line).read = true;
                break;
            case Op::ADD: case Op::SUB: case Op::MUL: case Op::DIV:
            case Op::MOD: case Op::POW: case Op::IDIV:
            case Op::BAND: case Op::BOR: case Op::BXOR:
            case Op::SHL: case Op::SHR:
                states.get(A, line).written = true;
                if (!vfIsConstRef(B)) states.get(B, line).read = true;
                if (!vfIsConstRef(C)) states.get(C, line).read = true;
                break;
            case Op::SELF:
                states.get(A, line).written = true;
                if (A + 1 < regCount) states.get(A + 1, line).written = true;
                states.get(B, line).read = true;
                if (!vfIsConstRef(C)) states.get(C, line).read = true;
                break;
            case Op::UNM: case Op::NOT: case Op::LEN: case Op::BNOT:
                states.get(A, line).written = true;
                states.get(B, line).read = true;
                break;
            case Op::CONCAT:
                states.get(A, line).written = true;
                for (int r = B; r <= C && r < regCount; r++) {
                    states.get(r, line).read = true;
                    states.setTemporary(r, line);
                }
                break;
            case Op::SETLIST:
                if (A + 1 < regCount) states.setTemporary(A + 1, line);
                break;
            case Op::JMP:
                break;
            case Op::EQ: case Op::LT: case Op::LE:
                if (!vfIsConstRef(B)) states.get(B, line).read = true;
                if (!vfIsConstRef(C)) states.get(C, line).read = true;
                break;
            case Op::TEST:
                states.get(A, line).read = true;
                break;
            case Op::TESTSET:
                states.get(B, line).read = true;
                break;
            case Op::CALL:
            case Op::TAILCALL: {
                int bVal = B;
                int cVal = C;
                if (op != Op::TAILCALL && cVal >= 2) {
                    for (int r = A; r <= A + cVal - 2 && r < regCount; r++) {
                        states.get(r, line).written = true;
                    }
                }
                int argTop = (bVal == 0) ? regCount - 1 : (A + bVal - 1);
                for (int r = A; r <= argTop && r < regCount; r++) {
                    states.get(r, line).read = true;
                }
                states.setTemporary(A, line);
                if (cVal >= 2) {
                    int nline = line + 1;
                    int r = A + cVal - 2;
                    while (r >= A && nline <= length) {
                        if (opAt(nline) == Op::MOVE && B_(nline) == r) {
                            states.get(A_(nline), nline).written = true;
                            states.get(B_(nline), nline).read = true;
                            states.setLocal(A_(nline), nline);
                            skip[(size_t)nline] = true;
                        }
                        r--;
                        nline++;
                    }
                }
                break;
            }
            case Op::RETURN: {
                int bVal = B;
                int argTop = (bVal == 0) ? regCount - 1 : (A + bVal - 2);
                for (int r = A; r <= argTop && r < regCount; r++) {
                    states.get(r, line).read = true;
                }
                break;
            }
            case Op::FORLOOP:
            case Op::FORPREP:
                states.get(A, line).written = true;
                states.get(A, line).read = true;
                if (A + 1 < regCount) states.get(A + 1, line).read = true;
                if (A + 2 < regCount) states.get(A + 2, line).read = true;
                if (A + 3 < regCount) {
                    states.get(A + 3, line).written = true;
                    states.setLocal(A + 3, line);
                }
                break;
            case Op::TFORLOOP:
                states.get(A, line).read = true;
                if (A + 1 < regCount) states.get(A + 1, line).read = true;
                if (A + 2 < regCount) states.get(A + 2, line).read = true;
                for (int r = A + 3; r <= A + 2 + C && r < regCount; r++) {
                    states.get(r, line).written = true;
                }
                if (A + 2 + C < regCount) states.setLocal(A + 2 + C, line);
                break;
            case Op::CLOSURE: {
                states.get(A, line).written = true;
                int idx = Bx_(line);
                if (idx >= 0 && idx < (int)f.functions.size()) {
                    int numUp = f.functions[(size_t)idx]->numUpvalues;
                    for (int u = 0; u < numUp; u++) {
                        int uline = line + 1 + u;
                        if (uline <= length) {
                            Op uop = opAt(uline);
                            if (uop == Op::MOVE) {
                                int srcReg = B_(uline);
                                if (srcReg >= 0 && srcReg < regCount) {
                                    states.get(srcReg, line).read = true;
                                    states.setLocal(srcReg, line);
                                }
                            }
                            skip[(size_t)uline] = true;
                        }
                    }
                }
                break;
            }
            case Op::VARARG: {
                int bVal = B;
                int top = (bVal == 0) ? regCount - 1 : (A + bVal - 2);
                for (int r = A; r <= top && r < regCount; r++) {
                    states.get(r, line).written = true;
                }
                break;
            }
            case Op::CLOSE:
                break;
            default:
                break;
        }
    }

    std::vector<bool> inLoop((size_t)length + 2, false);
    for (int line = 1; line <= length; line++) {
        if (opAt(line) == Op::JMP) {
            int sBx = ex.extract_sBx(f.code[(size_t)(line - 1)]);
            if (sBx < 0) {
                int target = line + 1 + sBx;
                for (int l = target; l <= line && l <= length; l++) {
                    if (l >= 1) inLoop[(size_t)l] = true;
                }
            }
        } else if (opAt(line) == Op::FORLOOP || opAt(line) == Op::TFORLOOP) {
            int sBx = ex.extract_sBx(f.code[(size_t)(line - 1)]);
            if (sBx < 0) {
                int target = line + 1 + sBx;
                for (int l = target; l <= line && l <= length; l++) {
                    if (l >= 1) inLoop[(size_t)l] = true;
                }
            }
        }
    }

    for (int reg = 0; reg < regCount; reg++) {
        bool isLocal = false;
        bool isTemp = false;
        int readCount = 0;
        int writtenCount = 0;
        int firstWrite = -1;
        bool writeInLoop = false;
        bool sameLineReadWrite = false;
        if (reg < numParams) {
            isLocal = true;
        }
        for (int line = 1; line <= length; line++) {
            VFState& s = states.get(reg, line);
            if (s.local) isLocal = true;
            if (s.temporary) isTemp = true;
            if (s.read) readCount++;
            if (s.written) {
                writtenCount++;
                if (firstWrite < 0) firstWrite = line;
                if (inLoop[(size_t)line]) writeInLoop = true;
            }
            if (s.read && s.written) sameLineReadWrite = true;
        }
        if (!isLocal) {
            if (writeInLoop && writtenCount > 0 && (sameLineReadWrite || readCount > writtenCount)) {
                isLocal = true;
            } else if (!isTemp && writtenCount == 1 && readCount >= 1) {
                isLocal = true;
            } else if (!isTemp && readCount > writtenCount && readCount >= 2) {
                isLocal = true;
            }
        }
        if (isLocal) {
            auto d = std::make_shared<Declaration>();
            if (reg < numParams) {
                d->name = "arg" + std::to_string(reg);
            } else {
                d->name = "L" + std::to_string(reg);
            }
            d->begin = 0;
            d->end = length - 1;
            if (d->end < d->begin) d->end = d->begin;
            d->reg = reg;
            (void)firstWrite;
            result.push_back(d);
        }
    }
    return result;
}

