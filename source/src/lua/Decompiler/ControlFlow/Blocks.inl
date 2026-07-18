class OuterBlock : public Block {
public:
    std::vector<StatementPtr> statements;
    OuterBlock(const LFunction* fn, int len) : Block(fn, 0, len + 1) {
        statements.reserve((size_t)len);
    }
    void addStatement(StatementPtr s) override { statements.push_back(s); }
    bool breakable() override { return false; }
    bool isContainer() override { return true; }
    bool isUnprotected() override { return false; }
    int getLoopback() override { throw std::runtime_error("OuterBlock getLoopback"); }
    int scopeEnd() const override { return end - 1 - 1; }
    void print(Decompiler& d, Output& out) override;
};

class IfThenEndBlock : public Block {
public:
    BranchPtr branch;
    std::vector<StatementPtr> statements;
    Registers& r;
    std::shared_ptr<Stack<BranchPtr>> stack;
    IfThenEndBlock(const LFunction* fn, BranchPtr br, Registers& reg)
        : Block(fn, br->begin, br->end), branch(br), r(reg) {}
    IfThenEndBlock(const LFunction* fn, BranchPtr br, std::shared_ptr<Stack<BranchPtr>> st, Registers& reg)
        : Block(fn, br->begin, br->end), branch(br), r(reg), stack(st) {
        if (br->begin == br->end) {
            begin = br->begin - 1;
            end = br->begin - 1;
        }
    }
    void addStatement(StatementPtr s) override { statements.push_back(s); }
    bool breakable() override { return false; }
    bool isContainer() override { return true; }
    bool isUnprotected() override { return false; }
    int getLoopback() override { throw std::runtime_error("IfThenEndBlock getLoopback"); }
    void print(Decompiler& d, Output& out) override;
};

class ElseEndBlock;

class IfThenElseBlock : public Block {
public:
    BranchPtr branch;
    int loopback;
    bool emptyElse;
    std::vector<StatementPtr> statements;
    Registers& r;
    std::shared_ptr<ElseEndBlock> partner;
    IfThenElseBlock(const LFunction* fn, BranchPtr br, int lb, bool ee, Registers& reg)
        : Block(fn, br->begin, br->end), branch(br), loopback(lb), emptyElse(ee), r(reg) {}
    void addStatement(StatementPtr s) override { statements.push_back(s); }
    bool breakable() override { return false; }
    bool isContainer() override { return true; }
    int scopeEnd() const override { return end - 2; }
    bool isUnprotected() override { return true; }
    int getLoopback() override { return loopback; }
    bool isIfThenElseBlock() override { return true; }
    void print(Decompiler& d, Output& out) override;
};

class ElseEndBlock : public Block {
public:
    std::vector<StatementPtr> statements;
    std::shared_ptr<IfThenElseBlock> partner;
    ElseEndBlock(const LFunction* fn, int b, int e) : Block(fn, b, e) {
        statements.reserve((size_t)(e - b + 1));
    }
    bool breakable() override { return false; }
    bool isContainer() override { return true; }
    void addStatement(StatementPtr s) override { statements.push_back(s); }
    bool isUnprotected() override { return false; }
    int getLoopback() override { throw std::runtime_error("ElseEndBlock getLoopback"); }
    void print(Decompiler& d, Output& out) override;
};

class DoEndBlock : public Block {
public:
    std::vector<StatementPtr> statements;
    DoEndBlock(const LFunction* fn, int b, int e) : Block(fn, b, e) {
        statements.reserve((size_t)(e - b + 1));
    }
    void addStatement(StatementPtr s) override { statements.push_back(s); }
    bool breakable() override { return false; }
    bool isContainer() override { return true; }
    bool isUnprotected() override { return false; }
    int getLoopback() override { throw std::runtime_error("DoEndBlock getLoopback"); }
    void print(Decompiler& d, Output& out) override;
};

class WhileBlock : public Block {
public:
    BranchPtr branch;
    int loopback;
    std::vector<StatementPtr> statements;
    Registers& r;
    WhileBlock(const LFunction* fn, BranchPtr br, int lb, Registers& reg)
        : Block(fn, br->begin, br->end), branch(br), loopback(lb), r(reg) {}
    int scopeEnd() const override { return end - 2; }
    bool breakable() override { return true; }
    bool isContainer() override { return true; }
    void addStatement(StatementPtr s) override { statements.push_back(s); }
    bool isUnprotected() override { return true; }
    int getLoopback() override { return loopback; }
    void print(Decompiler& d, Output& out) override;
};

class RepeatBlock : public Block {
public:
    BranchPtr branch;
    std::vector<StatementPtr> statements;
    Registers& r;
    RepeatBlock(const LFunction* fn, BranchPtr br, Registers& reg)
        : Block(fn, br->end, br->begin), branch(br), r(reg) {
        statements.reserve((size_t)(br->begin - br->end + 1));
    }
    bool breakable() override { return true; }
    bool isContainer() override { return true; }
    void addStatement(StatementPtr s) override { statements.push_back(s); }
    bool isUnprotected() override { return false; }
    int getLoopback() override { throw std::runtime_error("RepeatBlock getLoopback"); }
    void print(Decompiler& d, Output& out) override;
};

class ForBlock : public Block {
public:
    int reg;
    Registers& r;
    std::vector<StatementPtr> statements;
    ForBlock(const LFunction* fn, int b, int e, int reg_, Registers& reg)
        : Block(fn, b, e), reg(reg_), r(reg) {}
    int scopeEnd() const override { return end - 2; }
    void addStatement(StatementPtr s) override { statements.push_back(s); }
    bool breakable() override { return true; }
    bool isContainer() override { return true; }
    bool isUnprotected() override { return false; }
    int getLoopback() override { throw std::runtime_error("ForBlock getLoopback"); }
    void print(Decompiler& d, Output& out) override;
};

class TForBlock : public Block {
public:
    int reg;
    int len;
    Registers& r;
    std::vector<StatementPtr> statements;
    TForBlock(const LFunction* fn, int b, int e, int reg_, int l, Registers& reg)
        : Block(fn, b, e), reg(reg_), len(l), r(reg) {}
    int scopeEnd() const override { return end - 3; }
    bool breakable() override { return true; }
    bool isContainer() override { return true; }
    void addStatement(StatementPtr s) override { statements.push_back(s); }
    bool isUnprotected() override { return false; }
    int getLoopback() override { throw std::runtime_error("TForBlock getLoopback"); }
    void print(Decompiler& d, Output& out) override;
};

class AlwaysLoop : public Block {
public:
    std::vector<StatementPtr> statements;
    AlwaysLoop(const LFunction* fn, int b, int e) : Block(fn, b, e) {}
    int scopeEnd() const override { return end - 2; }
    bool breakable() override { return true; }
    bool isContainer() override { return true; }
    bool isUnprotected() override { return true; }
    int getLoopback() override { return begin; }
    void addStatement(StatementPtr s) override { statements.push_back(s); }
    void print(Decompiler& d, Output& out) override;
};

class Break : public Block {
public:
    int target;
    Break(const LFunction* fn, int line, int tgt) : Block(fn, line, line), target(tgt) {}
    void addStatement(StatementPtr) override { throw std::runtime_error("Break addStatement"); }
    bool breakable() override { return false; }
    bool isContainer() override { return false; }
    bool isUnprotected() override { return false; }
    int getLoopback() override { throw std::runtime_error("Break getLoopback"); }
    void print(Decompiler& d, Output& out) override { out.print("do break end"); }
    void printTail(Decompiler& d, Output& out) override { out.print("break"); }
};

class BooleanIndicator : public Block {
public:
    BooleanIndicator(const LFunction* fn, int line) : Block(fn, line, line) {}
    void addStatement(StatementPtr) override {}
    bool breakable() override { return false; }
    bool isContainer() override { return false; }
    bool isUnprotected() override { return false; }
    int getLoopback() override { throw std::runtime_error("BooleanIndicator getLoopback"); }
    void print(Decompiler& d, Output& out) override { out.print("-- unhandled boolean indicator"); }
};

