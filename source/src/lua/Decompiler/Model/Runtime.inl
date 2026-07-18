class Function {
public:
    const LFunction& f;
    std::vector<Constant> constants;
    int constantsOffset = 256;

    Function(const LFunction& fn) : f(fn) {
        constants.reserve(fn.constants.size());
        for (auto& c : fn.constants) constants.push_back(Constant::fromLObject(c));
    }
    bool isConstant(int reg) const { return reg >= constantsOffset; }
    int constantIndex(int reg) const { return reg - constantsOffset; }
    std::string getGlobalName(int idx) const {
        if (idx < 0 || idx >= (int)constants.size()) return "_G_" + std::to_string(idx);
        return constants[(size_t)idx].asName();
    }
    Constant getConstant(int idx) const {
        if (idx < 0 || idx >= (int)constants.size()) return Constant();
        return constants[(size_t)idx];
    }
    ExpressionPtr getConstantExpression(int idx);
    ExpressionPtr getGlobalExpression(int idx);
};

class Upvalues {
public:
    std::vector<std::string> names;
    Upvalues(const LFunction& f, const std::vector<DeclarationPtr>* parentDecls, int line) {
        names.resize((size_t)f.numUpvalues);
        for (int i = 0; i < f.numUpvalues; i++) {
            if (i < (int)f.upvalues.size() && !f.upvalues[(size_t)i].name.empty()) {
                names[(size_t)i] = f.upvalues[(size_t)i].name;
            } else if (parentDecls && i < (int)parentDecls->size()) {
                names[(size_t)i] = (*parentDecls)[(size_t)i]->name;
            } else {
                names[(size_t)i] = "_UPVALUE" + std::to_string(i) + "_";
            }
        }
    }
    std::string getName(int idx) {
        if (idx < 0 || idx >= (int)names.size() || names[(size_t)idx].empty())
            return "_UPVALUE" + std::to_string(idx) + "_";
        return names[(size_t)idx];
    }
    ExpressionPtr getExpression(int idx);
};

class Target {
public:
    virtual ~Target() = default;
    virtual void print(Decompiler& d, Output& out) = 0;
    virtual void printMethod(Decompiler& d, Output& out) {
        throw std::runtime_error("not a method target");
    }
    virtual bool isDeclaration(DeclarationPtr decl) { return false; }
    virtual bool isLocal() { return false; }
    virtual int getIndex() { return -1; }
    virtual bool isFunctionName() { return true; }
    virtual bool beginsWithParen() { return false; }
    virtual bool equals(TargetPtr other) { return false; }
};

class Expression;
using ExpressionPtrLocal = std::shared_ptr<Expression>;

struct TableLiteralEntry {
    std::shared_ptr<Expression> key;
    std::shared_ptr<Expression> value;
    bool isList;
    int timestamp;
};

class Expression : public std::enable_shared_from_this<Expression> {
public:
    static constexpr int PREC_OR = 1;
    static constexpr int PREC_AND = 2;
    static constexpr int PREC_COMPARE = 3;
    static constexpr int PREC_BOR = 4;
    static constexpr int PREC_BXOR = 5;
    static constexpr int PREC_BAND = 6;
    static constexpr int PREC_SHIFT = 7;
    static constexpr int PREC_CONCAT = 8;
    static constexpr int PREC_ADD = 9;
    static constexpr int PREC_MUL = 10;
    static constexpr int PREC_UNARY = 11;
    static constexpr int PREC_POW = 12;
    static constexpr int PREC_ATOMIC = 13;
    static constexpr int ASSOC_NONE = 0;
    static constexpr int ASSOC_LEFT = 1;
    static constexpr int ASSOC_RIGHT = 2;

    int precedence;
    Expression(int p) : precedence(p) {}
    virtual ~Expression() = default;
    virtual void print(Decompiler& d, Output& out) = 0;
    virtual void printBraced(Decompiler& d, Output& out) { print(d, out); }
    virtual void printMultiple(Decompiler& d, Output& out) { print(d, out); }
    virtual int getConstantIndex() = 0;
    virtual bool beginsWithParen() { return false; }
    virtual bool isNil() { return false; }
    virtual bool isClosure() { return false; }
    virtual bool isConstant() { return false; }
    virtual bool isUngrouped() { return false; }
    virtual bool isUpvalueOf(int reg) { return false; }
    virtual bool isBoolean() { return false; }
    virtual bool isInteger() { return false; }
    virtual int asInteger() { return 0; }
    virtual bool isString() { return false; }
    virtual bool isIdentifier() { return false; }
    virtual bool isDotChain() { return false; }
    virtual int closureUpvalueLine() { return -1; }
    virtual void printClosure(Decompiler& d, Output& out, TargetPtr name) {}
    virtual std::string asName() { return ""; }
    virtual bool isTableLiteral() { return false; }
    virtual bool isNewEntryAllowed() { return false; }
    virtual void addEntry(TableLiteralEntry entry) {}
    virtual bool isMultiple() { return false; }
    virtual bool isMemberAccess() { return false; }
    virtual ExpressionPtr getTable() { return nullptr; }
    virtual std::string getField() { return ""; }
    virtual bool isBrief() { return false; }
    virtual bool isEnvironmentTable(Decompiler& d) { return false; }
    static void printSequence(Decompiler& d, Output& out, const std::vector<ExpressionPtr>& exprs, bool linebreak, bool multiple);
};

class Statement : public std::enable_shared_from_this<Statement> {
public:
    std::string comment;
    virtual ~Statement() = default;
    virtual void print(Decompiler& d, Output& out) = 0;
    virtual void printTail(Decompiler& d, Output& out) { print(d, out); }
    virtual bool beginsWithParen() { return false; }
    void addComment(const std::string& c) { comment = c; }
    virtual bool isIfThenElseBlock() { return false; }
    static void printSequence(Decompiler& d, Output& out, std::vector<StatementPtr>& stmts);
};

class Operation {
public:
    int line;
    Operation(int l) : line(l) {}
    virtual ~Operation() = default;
    virtual StatementPtr process(Registers& r, BlockPtr block) = 0;
};

class Branch {
public:
    int line;
    int begin;
    int end;
    bool isSet = false;
    bool isCompareSet = false;
    bool isTest = false;
    int setTarget = -1;

    Branch(int l, int b, int e) : line(l), begin(b), end(e) {}
    virtual ~Branch() = default;
    virtual BranchPtr invert() = 0;
    virtual int getRegister() = 0;
    virtual ExpressionPtr asExpression(Registers& r) = 0;
    virtual void useExpression(ExpressionPtr expr) {}
};

class Block : public Statement {
public:
    const LFunction* function;
    int begin;
    int end;
    bool loopRedirectAdjustment = false;
    Block(const LFunction* fn, int b, int e) : function(fn), begin(b), end(e) {}
    virtual void addStatement(StatementPtr s) = 0;
    bool contains(BlockPtr b) const { return begin <= b->begin && end >= b->end; }
    bool contains(int line) const { return begin <= line && line < end; }
    virtual int scopeEnd() const { return end - 1; }
    virtual bool isUnprotected() = 0;
    virtual int getLoopback() = 0;
    virtual bool breakable() = 0;
    virtual bool isContainer() = 0;
    virtual OperationPtr process(Decompiler& d);
};

static int compareBlocks(BlockPtr a, BlockPtr b);

class Registers {
public:
    int regCount;
    int length;
    Function& f;
    std::vector<std::vector<DeclarationPtr>> decls;
    std::vector<std::vector<ExpressionPtr>> values;
    std::vector<std::vector<int>> updated;
    std::vector<bool> startedLines;

    Registers(int regs, int len, std::vector<DeclarationPtr>& declList, Function& fn)
        : regCount(regs), length(len), f(fn)
    {
        if (regs < 1) regs = 1;
        if (len < 0) len = 0;
        regCount = regs;
        length = len;
        decls.assign((size_t)regs, std::vector<DeclarationPtr>((size_t)(len + 1), nullptr));
        for (auto& decl : declList) {
            if (decl->begin < 0) decl->begin = 0;
            if (decl->end > len) decl->end = len;
            if (decl->begin > decl->end) continue;
            if (decl->reg < 0) {
                int reg = 0;
                while (reg < regs && decls[(size_t)reg][(size_t)decl->begin]) reg++;
                if (reg >= regs) reg = regs - 1;
                decl->reg = reg;
            } else if (decl->reg >= regs) {
                decl->reg = regs - 1;
            }
            for (int line = decl->begin; line <= decl->end && line <= len; line++) {
                decls[(size_t)decl->reg][(size_t)line] = decl;
            }
        }
        values.assign((size_t)regs, std::vector<ExpressionPtr>((size_t)(len + 1), nullptr));
        updated.assign((size_t)regs, std::vector<int>((size_t)(len + 1), 0));
        startedLines.assign((size_t)(len + 1), false);
    }
    bool isLocal(int reg, int line) const {
        if (reg < 0 || reg >= regCount) return false;
        if (line < 0 || line > length) return false;
        return decls[(size_t)reg][(size_t)line] != nullptr;
    }
    bool isAssignable(int reg, int line) const {
        if (!isLocal(reg, line)) return false;
        return !decls[(size_t)reg][(size_t)line]->forLoop;
    }
    bool isNewLocal(int reg, int line) const {
        DeclarationPtr d = (reg >= 0 && reg < regCount && line >= 0 && line <= length)
            ? decls[(size_t)reg][(size_t)line] : nullptr;
        return d && d->begin == line && !d->forLoop;
    }
    std::vector<DeclarationPtr> getNewLocals(int line) {
        std::vector<DeclarationPtr> result;
        for (int reg = 0; reg < regCount; reg++) {
            if (isNewLocal(reg, line)) result.push_back(decls[(size_t)reg][(size_t)line]);
        }
        return result;
    }
    DeclarationPtr getDeclaration(int reg, int line) const {
        if (reg < 0 || reg >= regCount || line < 0 || line > length) return nullptr;
        return decls[(size_t)reg][(size_t)line];
    }
    void startLine(int line) {
        if (line <= 0 || line > length) return;
        startedLines[(size_t)line] = true;
        for (int reg = 0; reg < regCount; reg++) {
            values[(size_t)reg][(size_t)line] = values[(size_t)reg][(size_t)(line - 1)];
            updated[(size_t)reg][(size_t)line] = updated[(size_t)reg][(size_t)(line - 1)];
        }
    }
    ExpressionPtr getExpression(int reg, int line);
    ExpressionPtr getKExpression(int reg, int line);
    ExpressionPtr getValue(int reg, int line) {
        if (reg < 0 || reg >= regCount || line < 0 || line > length) return nullptr;
        return values[(size_t)reg][(size_t)(line - 1)];
    }
    int getUpdated(int reg, int line) const {
        if (reg < 0 || reg >= regCount || line < 0 || line > length) return 0;
        return updated[(size_t)reg][(size_t)line];
    }
    void setValue(int reg, int line, ExpressionPtr expr) {
        if (reg < 0 || reg >= regCount || line < 0 || line > length) return;
        values[(size_t)reg][(size_t)line] = expr;
        updated[(size_t)reg][(size_t)line] = line;
    }
    TargetPtr getTarget(int reg, int line);
    void setInternalLoopVariable(int reg, int begin, int end) {
        if (reg < 0 || reg >= regCount) return;
        if (begin < 0) begin = 0;
        if (end > length) end = length;
        DeclarationPtr d = getDeclaration(reg, begin);
        if (!d) {
            d = std::make_shared<Declaration>();
            d->name = "_FOR_";
            d->begin = begin;
            d->end = end;
            d->reg = reg;
            for (int line = begin; line <= end && line <= length; line++) {
                decls[(size_t)reg][(size_t)line] = d;
            }
        }
        d->forLoop = true;
    }
    void setExplicitLoopVariable(int reg, int begin, int end, const std::string& nicename = "") {
        if (reg < 0 || reg >= regCount) return;
        if (begin < 0) begin = 0;
        if (end > length) end = length;
        DeclarationPtr d = getDeclaration(reg, begin);
        if (!d) {
            d = std::make_shared<Declaration>();
            d->name = nicename.empty() ? ("_FORV_" + std::to_string(reg) + "_") : nicename;
            d->begin = begin;
            d->end = end;
            d->reg = reg;
            for (int line = begin; line <= end && line <= length; line++) {
                decls[(size_t)reg][(size_t)line] = d;
            }
        } else if (!nicename.empty() && !d->name.empty() && (d->name[0] == 'L' || d->name.substr(0, 5) == "_FORV")) {
            d->name = nicename;
        }
        d->forLoopExplicit = true;
    }
};

