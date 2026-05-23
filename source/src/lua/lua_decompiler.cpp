#include "lua_decompile.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace lua {

namespace {

class Decompiler;
class Expression; using ExpressionPtr = std::shared_ptr<Expression>;
class Target;     using TargetPtr     = std::shared_ptr<Target>;
class Statement;  using StatementPtr  = std::shared_ptr<Statement>;
class Operation;  using OperationPtr  = std::shared_ptr<Operation>;
class Branch;     using BranchPtr     = std::shared_ptr<Branch>;
class Block;      using BlockPtr      = std::shared_ptr<Block>;
class Function;
class Registers;
class Upvalues;
class TableLiteral;

struct Declaration {
    std::string name;
    int begin = 0;
    int end = 0;
    int reg = -1;
    bool forLoop = false;
    bool forLoopExplicit = false;
};
using DeclarationPtr = std::shared_ptr<Declaration>;

template<typename T>
class Stack {
public:
    bool empty() const { return data_.empty(); }
    T& peek() { return data_.back(); }
    const T& peek() const { return data_.back(); }
    void push(const T& v) { data_.push_back(v); }
    T pop() { T v = std::move(data_.back()); data_.pop_back(); return v; }
    void reverse() { std::reverse(data_.begin(), data_.end()); }
    size_t size() const { return data_.size(); }
    typename std::vector<T>::iterator begin() { return data_.begin(); }
    typename std::vector<T>::iterator end() { return data_.end(); }
private:
    std::vector<T> data_;
};

class Output {
public:
    std::ostringstream os;
    int indent_level = 0;
    int position = 0;
    void start() {
        if (position == 0) {
            for (int i = 0; i < indent_level; i++) os << ' ';
            position = indent_level;
        }
    }
    void print(const std::string& s) { start(); os << s; position += (int)s.size(); }
    void print(const char* s) { start(); os << s; position += (int)std::strlen(s); }
    void print(char c) { start(); os.put(c); position++; }
    void println() { start(); os << '\n'; position = 0; }
    void println(const std::string& s) { print(s); println(); }
    void indent() { indent_level += 2; }
    void dedent() { indent_level -= 2; }
    std::string str() const { return os.str(); }
};

struct Constant {
    int type = 0;
    bool b = false;
    double n = 0.0;
    std::string s;

    Constant() = default;
    static Constant fromLObject(const LObject& o) {
        Constant c;
        switch (o.tag) {
            case LObject::NIL:    c.type = 0; break;
            case LObject::BOOL:   c.type = 1; c.b = o.b; break;
            case LObject::NUMBER: c.type = 2; c.n = o.n; break;
            case LObject::STRING: c.type = 3; c.s = o.s.value; break;
        }
        return c;
    }
    static Constant fromInt(int v) {
        Constant c; c.type = 2; c.n = (double)v; return c;
    }

    bool isNil()     const { return type == 0; }
    bool isBoolean() const { return type == 1; }
    bool isNumber()  const { return type == 2; }
    bool isString()  const { return type == 3; }
    bool isInteger() const {
        if (type != 2) return false;
        if (n != n) return false;
        return n == (double)(int64_t)n;
    }
    int asInteger()  const { return (int)(int64_t)n; }
    const std::string& asName() const { return s; }
    bool isIdentifier() const;
    void print(Output& out, bool braced) const;
};

static const std::unordered_set<std::string>& luaReservedWords() {
    static const std::unordered_set<std::string> kReserved = {
        "and","break","do","else","elseif","end","false","for","function","if",
        "in","local","nil","not","or","repeat","return","then","true","until","while",
        "goto"
    };
    return kReserved;
}

static bool isLuaIdentifierString(const std::string& s) {
    if (s.empty()) return false;
    if (luaReservedWords().count(s)) return false;
    char c = s[0];
    if (!(c == '_' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))) return false;
    for (size_t i = 1; i < s.size(); i++) {
        char ch = s[i];
        if (!(ch == '_' || (ch >= '0' && ch <= '9') ||
              (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z'))) return false;
    }
    return true;
}

bool Constant::isIdentifier() const {
    if (type != 3) return false;
    return isLuaIdentifierString(s);
}

void Constant::print(Output& out, bool /*braced*/) const {
    switch (type) {
        case 0: out.print("nil"); return;
        case 1: out.print(b ? "true" : "false"); return;
        case 2: {
            std::ostringstream os; os.precision(14);
            if (n != n) { out.print("(0/0)"); return; }
            if (n > 1e308) { out.print("(1/0)"); return; }
            if (n < -1e308) { out.print("(-1/0)"); return; }
            if (n == (double)(int64_t)n && n >= -1e15 && n <= 1e15) os << (int64_t)n;
            else os << n;
            out.print(os.str());
            return;
        }
        case 3: {
            std::string esc;
            esc.reserve(s.size() + 2);
            esc += '"';
            for (unsigned char ch : s) {
                switch (ch) {
                    case '"':  esc += "\\\""; break;
                    case '\\': esc += "\\\\"; break;
                    case '\n': esc += "\\n";  break;
                    case '\r': esc += "\\r";  break;
                    case '\t': esc += "\\t";  break;
                    case '\a': esc += "\\a";  break;
                    case '\b': esc += "\\b";  break;
                    case '\f': esc += "\\f";  break;
                    case '\v': esc += "\\v";  break;
                    default:
                        if (ch < 32 || ch >= 127) {
                            char buf[8];
                            std::snprintf(buf, sizeof(buf), "\\%d", (int)ch);
                            esc += buf;
                        } else {
                            esc += (char)ch;
                        }
                }
            }
            esc += '"';
            out.print(esc);
            return;
        }
    }
}

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

class BinaryExpression : public Expression {
public:
    std::string op;
    ExpressionPtr left;
    ExpressionPtr right;
    int associativity;
    BinaryExpression(const std::string& o, ExpressionPtr l, ExpressionPtr r, int p, int a)
        : Expression(p), op(o), left(l), right(r), associativity(a) {}
    bool isUngrouped() override { return !beginsWithParen(); }
    int getConstantIndex() override {
        return std::max(left->getConstantIndex(), right->getConstantIndex());
    }
    bool beginsWithParen() override {
        return leftGroup() || left->beginsWithParen();
    }
    void print(Decompiler& d, Output& out) override {
        bool lg = leftGroup();
        bool rg = rightGroup();
        if (lg) { out.print("("); left->print(d, out); out.print(")"); }
        else    { left->print(d, out); }
        out.print(" "); out.print(op); out.print(" ");
        if (rg) { out.print("("); right->print(d, out); out.print(")"); }
        else    { right->print(d, out); }
    }
private:
    bool leftGroup() const {
        return precedence > left->precedence ||
               (precedence == left->precedence && associativity == ASSOC_RIGHT);
    }
    bool rightGroup() const {
        return precedence > right->precedence ||
               (precedence == right->precedence && associativity == ASSOC_LEFT);
    }
};

class UnaryExpression : public Expression {
public:
    std::string op;
    ExpressionPtr inner;
    UnaryExpression(const std::string& o, ExpressionPtr e, int p)
        : Expression(p), op(o), inner(e) {}
    bool isUngrouped() override { return true; }
    int getConstantIndex() override { return inner->getConstantIndex(); }
    void print(Decompiler& d, Output& out) override {
        out.print(op);
        if (precedence > inner->precedence) {
            out.print("("); inner->print(d, out); out.print(")");
        } else {
            inner->print(d, out);
        }
    }
};

class ConstantExpression : public Expression {
public:
    Constant constant;
    int index;
    ConstantExpression(const Constant& c, int idx)
        : Expression(PREC_ATOMIC), constant(c), index(idx) {}
    int getConstantIndex() override { return index; }
    void print(Decompiler& d, Output& out) override { constant.print(out, false); }
    void printBraced(Decompiler& d, Output& out) override { constant.print(out, true); }
    bool isConstant() override { return true; }
    bool isUngrouped() override { return true; }
    bool isNil() override { return constant.isNil(); }
    bool isBoolean() override { return constant.isBoolean(); }
    bool isInteger() override { return constant.isInteger(); }
    int asInteger() override { return constant.asInteger(); }
    bool isString() override { return constant.isString(); }
    bool isIdentifier() override { return constant.isIdentifier(); }
    std::string asName() override { return constant.asName(); }
    bool isBrief() override { return !constant.isString() || constant.asName().size() <= 10; }
};

class GlobalExpression : public Expression {
public:
    std::string name;
    int index;
    GlobalExpression(const std::string& n, int i)
        : Expression(PREC_ATOMIC), name(n), index(i) {}
    int getConstantIndex() override { return index; }
    bool isDotChain() override { return true; }
    void print(Decompiler& d, Output& out) override { out.print(name); }
    bool isBrief() override { return true; }
};

class LocalVariable : public Expression {
public:
    DeclarationPtr decl;
    LocalVariable(DeclarationPtr d) : Expression(PREC_ATOMIC), decl(d) {}
    int getConstantIndex() override { return -1; }
    bool isDotChain() override { return true; }
    void print(Decompiler& d, Output& out) override { out.print(decl->name); }
    bool isBrief() override { return true; }
};

class UpvalueExpression : public Expression {
public:
    std::string name;
    UpvalueExpression(const std::string& n) : Expression(PREC_ATOMIC), name(n) {}
    int getConstantIndex() override { return -1; }
    bool isDotChain() override { return true; }
    void print(Decompiler& d, Output& out) override { out.print(name); }
    bool isBrief() override { return true; }
};

class TableReference : public Expression {
public:
    ExpressionPtr table;
    ExpressionPtr index;
    TableReference(ExpressionPtr t, ExpressionPtr i)
        : Expression(PREC_ATOMIC), table(t), index(i) {}
    int getConstantIndex() override {
        return std::max(table->getConstantIndex(), index->getConstantIndex());
    }
    void print(Decompiler& d, Output& out) override {
        bool isGlobal = false;
        if (!isGlobal) {
            if (table->isUngrouped()) {
                out.print("("); table->print(d, out); out.print(")");
            } else {
                table->print(d, out);
            }
        }
        if (index->isIdentifier()) {
            if (!isGlobal) out.print(".");
            out.print(index->asName());
        } else {
            out.print("[");
            index->printBraced(d, out);
            out.print("]");
        }
    }
    bool isDotChain() override { return index->isIdentifier() && table->isDotChain(); }
    bool isMemberAccess() override { return index->isIdentifier(); }
    bool beginsWithParen() override { return table->isUngrouped() || table->beginsWithParen(); }
    ExpressionPtr getTable() override { return table; }
    std::string getField() override { return index->asName(); }
};

class Vararg : public Expression {
public:
    int length;
    bool multiple;
    Vararg(int l, bool m) : Expression(PREC_ATOMIC), length(l), multiple(m) {}
    int getConstantIndex() override { return -1; }
    void print(Decompiler& d, Output& out) override { out.print(multiple ? "..." : "(...)"); }
    void printMultiple(Decompiler& d, Output& out) override { out.print(multiple ? "..." : "(...)"); }
    bool isMultiple() override { return multiple; }
};

class FunctionCall : public Expression {
public:
    ExpressionPtr function;
    std::vector<ExpressionPtr> arguments;
    bool multiple;
    FunctionCall(ExpressionPtr fn, std::vector<ExpressionPtr> args, bool m)
        : Expression(PREC_ATOMIC), function(fn), arguments(std::move(args)), multiple(m) {}
    int getConstantIndex() override {
        int idx = function->getConstantIndex();
        for (auto& a : arguments) idx = std::max(idx, a->getConstantIndex());
        return idx;
    }
    bool isMultiple() override { return multiple; }
    void printMultiple(Decompiler& d, Output& out) override {
        if (!multiple) out.print("(");
        print(d, out);
        if (!multiple) out.print(")");
    }
    bool beginsWithParen() override {
        if (isMethodCall()) {
            ExpressionPtr obj = function->getTable();
            return obj->isUngrouped() || obj->beginsWithParen();
        }
        return function->isUngrouped() || function->beginsWithParen();
    }
    void print(Decompiler& d, Output& out) override;
    bool isMethodCall() const {
        if (!function->isMemberAccess() || arguments.empty()) return false;
        auto t = function->getTable();
        auto a = arguments[0];
        if (!t || !a) return false;
        if (t.get() == a.get()) return true;
        auto tl = std::dynamic_pointer_cast<LocalVariable>(t);
        auto al = std::dynamic_pointer_cast<LocalVariable>(a);
        if (tl && al && tl->decl == al->decl) return true;
        auto tg = std::dynamic_pointer_cast<GlobalExpression>(t);
        auto ag = std::dynamic_pointer_cast<GlobalExpression>(a);
        if (tg && ag && tg->name == ag->name) return true;
        auto tu = std::dynamic_pointer_cast<UpvalueExpression>(t);
        auto au = std::dynamic_pointer_cast<UpvalueExpression>(a);
        if (tu && au && tu->name == au->name) return true;
        return false;
    }
};

class TableLiteral : public Expression {
public:
    std::vector<TableLiteralEntry> entries;
    bool isObject = true;
    bool isList = true;
    int listLength = 1;
    int capacity;
    TableLiteral(int arraySize, int hashSize)
        : Expression(PREC_ATOMIC), capacity(arraySize + hashSize) {
        entries.reserve((size_t)capacity);
    }
    int getConstantIndex() override {
        int idx = -1;
        for (auto& e : entries) {
            idx = std::max(idx, e.key->getConstantIndex());
            idx = std::max(idx, e.value->getConstantIndex());
        }
        return idx;
    }
    bool isTableLiteral() override { return true; }
    bool isUngrouped() override { return true; }
    bool isNewEntryAllowed() override { return (int)entries.size() < capacity; }
    void addEntry(TableLiteralEntry entry) override {
        bool entryIsObject = entry.isList || (entry.key && entry.key->isIdentifier());
        isObject = isObject && entryIsObject;
        isList = isList && entry.isList;
        entries.push_back(std::move(entry));
    }
    bool isBrief() override { return false; }
    void print(Decompiler& d, Output& out) override;
};

class ClosureExpression : public Expression {
public:
    std::shared_ptr<LFunction> function;
    int upvalueLine;
    std::vector<DeclarationPtr> declList;
    std::vector<std::string> upvalueNames;
    ClosureExpression(std::shared_ptr<LFunction> f, std::vector<DeclarationPtr> dl, int line)
        : Expression(PREC_ATOMIC), function(f), upvalueLine(line), declList(std::move(dl)) {}
    int getConstantIndex() override { return -1; }
    bool isClosure() override { return true; }
    bool isUngrouped() override { return true; }
    bool isUpvalueOf(int reg) override {
        if (!function) return false;
        for (auto& uv : function->upvalues) {
            if (!uv.name.empty() && reg >= 0 && reg < (int)function->upvalues.size()) {
            }
        }
        return false;
    }
    int closureUpvalueLine() override { return upvalueLine; }
    void print(Decompiler& outer, Output& out) override;
    void printClosure(Decompiler& outer, Output& out, TargetPtr name) override;
    void printMainHelper(Output& out, Decompiler& sub, bool includeFirst);
};

class GlobalTarget : public Target {
public:
    std::string name;
    GlobalTarget(const std::string& n) : name(n) {}
    void print(Decompiler& d, Output& out) override { out.print(name); }
};

class UpvalueTarget : public Target {
public:
    std::string name;
    UpvalueTarget(const std::string& n) : name(n) {}
    void print(Decompiler& d, Output& out) override { out.print(name); }
};

class VariableTarget : public Target {
public:
    DeclarationPtr decl;
    VariableTarget(DeclarationPtr d) : decl(d) {}
    void print(Decompiler& d, Output& out) override { out.print(decl->name); }
    bool isDeclaration(DeclarationPtr d) override { return decl == d; }
    bool isLocal() override { return true; }
    int getIndex() override { return decl->reg; }
    bool equals(TargetPtr other) override {
        auto vt = std::dynamic_pointer_cast<VariableTarget>(other);
        return vt && vt->decl == decl;
    }
};

class TableTarget : public Target {
public:
    ExpressionPtr table;
    ExpressionPtr index;
    TableTarget(ExpressionPtr t, ExpressionPtr i) : table(t), index(i) {}
    void print(Decompiler& d, Output& out) override {
        auto ref = std::make_shared<TableReference>(table, index);
        ref->print(d, out);
    }
    void printMethod(Decompiler& d, Output& out) override {
        table->print(d, out);
        out.print(":");
        out.print(index->asName());
    }
    bool isFunctionName() override { return index->isIdentifier() && table->isDotChain(); }
    bool beginsWithParen() override { return table->isUngrouped() || table->beginsWithParen(); }
};

class Assignment : public Statement {
public:
    std::vector<TargetPtr> targets;
    std::vector<ExpressionPtr> values;
    bool allnil = true;
    bool declareFlag = false;
    int declareStart = 0;

    Assignment() {}
    Assignment(TargetPtr t, ExpressionPtr v) {
        targets.push_back(t);
        values.push_back(v);
        allnil = v && v->isNil();
    }
    bool beginsWithParen() override { return targets[0]->beginsWithParen(); }
    TargetPtr getFirstTarget() { return targets[0]; }
    ExpressionPtr getFirstValue() { return values[0]; }
    bool assignsTarget(DeclarationPtr decl) {
        for (auto& t : targets) if (t->isDeclaration(decl)) return true;
        return false;
    }
    int getArity() const { return (int)targets.size(); }
    void addFirst(TargetPtr target, ExpressionPtr value) {
        targets.insert(targets.begin(), target);
        values.insert(values.begin(), value);
        allnil = allnil && value && value->isNil();
    }
    void addLast(TargetPtr target, ExpressionPtr value) {
        for (size_t i = 0; i < targets.size(); i++) {
            if (targets[i]->equals(target)) {
                targets.erase(targets.begin() + i);
                values.erase(values.begin() + i);
                break;
            }
        }
        targets.push_back(target);
        values.push_back(value);
        allnil = allnil && value && value->isNil();
    }
    bool assignListEquals(const std::vector<DeclarationPtr>& decls) {
        if (decls.size() != targets.size()) return false;
        for (size_t i = 0; i < targets.size(); i++) {
            bool found = false;
            for (auto& d : decls) {
                if (targets[i]->isDeclaration(d)) { found = true; break; }
            }
            if (!found) return false;
        }
        return true;
    }
    void declare(int start) { declareFlag = true; declareStart = start; }
    void print(Decompiler& d, Output& out) override;
};

class Declare : public Statement {
public:
    std::vector<DeclarationPtr> decls;
    Declare(std::vector<DeclarationPtr> d) : decls(std::move(d)) {}
    void print(Decompiler& d, Output& out) override {
        out.print("local ");
        out.print(decls[0]->name);
        for (size_t i = 1; i < decls.size(); i++) {
            out.print(", ");
            out.print(decls[i]->name);
        }
    }
};

class FunctionCallStatement : public Statement {
public:
    std::shared_ptr<FunctionCall> call;
    FunctionCallStatement(std::shared_ptr<FunctionCall> c) : call(c) {}
    void print(Decompiler& d, Output& out) override { call->print(d, out); }
    bool beginsWithParen() override { return call->beginsWithParen(); }
};

class Return : public Statement {
public:
    std::vector<ExpressionPtr> values;
    Return() {}
    Return(ExpressionPtr v) { values.push_back(v); }
    Return(std::vector<ExpressionPtr> v) : values(std::move(v)) {}
    void print(Decompiler& d, Output& out) override {
        out.print("do ");
        printTail(d, out);
        out.print(" end");
    }
    void printTail(Decompiler& d, Output& out) override;
};

class CallOperation : public Operation {
public:
    std::shared_ptr<FunctionCall> call;
    CallOperation(int l, std::shared_ptr<FunctionCall> c) : Operation(l), call(c) {}
    StatementPtr process(Registers& r, BlockPtr block) override {
        return std::make_shared<FunctionCallStatement>(call);
    }
};

class GlobalSet : public Operation {
public:
    std::string global;
    ExpressionPtr value;
    GlobalSet(int l, const std::string& g, ExpressionPtr v) : Operation(l), global(g), value(v) {}
    StatementPtr process(Registers& r, BlockPtr block) override {
        return std::make_shared<Assignment>(std::make_shared<GlobalTarget>(global), value);
    }
};

class RegisterSet : public Operation {
public:
    int reg;
    ExpressionPtr value;
    RegisterSet(int l, int r, ExpressionPtr v) : Operation(l), reg(r), value(v) {}
    StatementPtr process(Registers& r, BlockPtr block) override {
        r.setValue(reg, line, value);
        if (r.isAssignable(reg, line)) {
            return std::make_shared<Assignment>(r.getTarget(reg, line), value);
        }
        return nullptr;
    }
};

class ReturnOperation : public Operation {
public:
    std::vector<ExpressionPtr> values;
    ReturnOperation(int l, ExpressionPtr v) : Operation(l) { values.push_back(v); }
    ReturnOperation(int l, std::vector<ExpressionPtr> v) : Operation(l), values(std::move(v)) {}
    StatementPtr process(Registers& r, BlockPtr block) override {
        return std::make_shared<Return>(values);
    }
};

static bool exprDeepEquals(const ExpressionPtr& a, const ExpressionPtr& b);

class TableSet : public Operation {
public:
    ExpressionPtr table;
    ExpressionPtr index;
    ExpressionPtr value;
    bool isTable;
    int timestamp;
    TableSet(int l, ExpressionPtr t, ExpressionPtr i, ExpressionPtr v, bool tb, int ts)
        : Operation(l), table(t), index(i), value(v), isTable(tb), timestamp(ts) {}
    StatementPtr process(Registers& r, BlockPtr block) override {
        if (table->isTableLiteral() && (value->isMultiple() || table->isNewEntryAllowed())) {
            TableLiteralEntry e;
            e.key = index;
            e.value = value;
            e.isList = !isTable;
            e.timestamp = timestamp;
            table->addEntry(e);
            return nullptr;
        }
        auto assign = std::make_shared<Assignment>(std::make_shared<TableTarget>(table, index), value);
        if (value) {
            auto vt = std::dynamic_pointer_cast<TableReference>(value);
            if (vt && exprDeepEquals(vt->table, table) && exprDeepEquals(vt->index, index)) {
                assign->comment = "or-pattern (TESTSET unrecovered)";
            }
        }
        return assign;
    }
};

static bool exprDeepEquals(const ExpressionPtr& a, const ExpressionPtr& b) {
    if (a.get() == b.get()) return true;
    if (!a || !b) return false;
    auto la = std::dynamic_pointer_cast<LocalVariable>(a);
    auto lb = std::dynamic_pointer_cast<LocalVariable>(b);
    if (la && lb) return la->decl == lb->decl;
    auto ga = std::dynamic_pointer_cast<GlobalExpression>(a);
    auto gb = std::dynamic_pointer_cast<GlobalExpression>(b);
    if (ga && gb) return ga->name == gb->name;
    auto ua = std::dynamic_pointer_cast<UpvalueExpression>(a);
    auto ub = std::dynamic_pointer_cast<UpvalueExpression>(b);
    if (ua && ub) return ua->name == ub->name;
    auto ca = std::dynamic_pointer_cast<ConstantExpression>(a);
    auto cb = std::dynamic_pointer_cast<ConstantExpression>(b);
    if (ca && cb) {
        if (ca->constant.type != cb->constant.type) return false;
        if (ca->constant.type == 3) return ca->constant.s == cb->constant.s;
        if (ca->constant.type == 2) return ca->constant.n == cb->constant.n;
        if (ca->constant.type == 1) return ca->constant.b == cb->constant.b;
        return true;
    }
    auto ta = std::dynamic_pointer_cast<TableReference>(a);
    auto tb = std::dynamic_pointer_cast<TableReference>(b);
    if (ta && tb) return exprDeepEquals(ta->table, tb->table) && exprDeepEquals(ta->index, tb->index);
    return false;
}

class UpvalueSet : public Operation {
public:
    std::shared_ptr<UpvalueTarget> target;
    ExpressionPtr value;
    UpvalueSet(int l, const std::string& upv, ExpressionPtr v)
        : Operation(l), target(std::make_shared<UpvalueTarget>(upv)), value(v) {}
    StatementPtr process(Registers& r, BlockPtr block) override {
        return std::make_shared<Assignment>(target, value);
    }
};

class AndBranch : public Branch {
public:
    BranchPtr left, right;
    AndBranch(BranchPtr l, BranchPtr r) : Branch(r->line, r->begin, r->end), left(l), right(r) {}
    BranchPtr invert() override;
    int getRegister() override {
        int lr = left->getRegister(), rr = right->getRegister();
        return (lr == rr) ? lr : -1;
    }
    ExpressionPtr asExpression(Registers& r) override {
        return std::make_shared<BinaryExpression>("and",
            left->asExpression(r), right->asExpression(r),
            Expression::PREC_AND, Expression::ASSOC_NONE);
    }
    void useExpression(ExpressionPtr expr) override {
        left->useExpression(expr); right->useExpression(expr);
    }
};

class OrBranch : public Branch {
public:
    BranchPtr left, right;
    OrBranch(BranchPtr l, BranchPtr r) : Branch(r->line, r->begin, r->end), left(l), right(r) {}
    BranchPtr invert() override;
    int getRegister() override {
        int lr = left->getRegister(), rr = right->getRegister();
        return (lr == rr) ? lr : -1;
    }
    ExpressionPtr asExpression(Registers& r) override {
        return std::make_shared<BinaryExpression>("or",
            left->asExpression(r), right->asExpression(r),
            Expression::PREC_OR, Expression::ASSOC_NONE);
    }
    void useExpression(ExpressionPtr expr) override {
        left->useExpression(expr); right->useExpression(expr);
    }
};

BranchPtr AndBranch::invert() {
    return std::make_shared<OrBranch>(left->invert(), right->invert());
}
BranchPtr OrBranch::invert() {
    return std::make_shared<AndBranch>(left->invert(), right->invert());
}

class NotBranch : public Branch {
public:
    BranchPtr inner;
    NotBranch(BranchPtr b) : Branch(b->line, b->begin, b->end), inner(b) {}
    BranchPtr invert() override { return inner; }
    int getRegister() override { return inner->getRegister(); }
    ExpressionPtr asExpression(Registers& r) override {
        return std::make_shared<UnaryExpression>("not ", inner->asExpression(r), Expression::PREC_UNARY);
    }
};

class TestNode : public Branch {
public:
    int test;
    bool invertFlag;
    TestNode(int t, bool inv, int line, int begin, int end)
        : Branch(line, begin, end), test(t), invertFlag(inv) {
        isTest = true;
    }
    BranchPtr invert() override {
        return std::make_shared<TestNode>(test, !invertFlag, line, end, begin);
    }
    int getRegister() override { return test; }
    ExpressionPtr asExpression(Registers& r) override;
};

class TestSetNode : public Branch {
public:
    int test;
    bool invertFlag;
    TestSetNode(int target, int t, bool inv, int line, int begin, int end)
        : Branch(line, begin, end), test(t), invertFlag(inv) {
        setTarget = target;
    }
    BranchPtr invert() override {
        return std::make_shared<TestSetNode>(setTarget, test, !invertFlag, line, end, begin);
    }
    int getRegister() override { return setTarget; }
    ExpressionPtr asExpression(Registers& r) override;
};

class EQNode : public Branch {
public:
    int left, right;
    bool invertFlag;
    EQNode(int l, int r, bool inv, int line, int begin, int end)
        : Branch(line, begin, end), left(l), right(r), invertFlag(inv) {}
    BranchPtr invert() override {
        return std::make_shared<EQNode>(left, right, !invertFlag, line, end, begin);
    }
    int getRegister() override { return -1; }
    ExpressionPtr asExpression(Registers& r) override;
};

class LTNode : public Branch {
public:
    int left, right;
    bool invertFlag;
    LTNode(int l, int r, bool inv, int line, int begin, int end)
        : Branch(line, begin, end), left(l), right(r), invertFlag(inv) {}
    BranchPtr invert() override {
        return std::make_shared<LTNode>(left, right, !invertFlag, line, end, begin);
    }
    int getRegister() override { return -1; }
    ExpressionPtr asExpression(Registers& r) override;
};

class LENode : public Branch {
public:
    int left, right;
    bool invertFlag;
    LENode(int l, int r, bool inv, int line, int begin, int end)
        : Branch(line, begin, end), left(l), right(r), invertFlag(inv) {}
    BranchPtr invert() override {
        return std::make_shared<LENode>(left, right, !invertFlag, line, end, begin);
    }
    int getRegister() override { return -1; }
    ExpressionPtr asExpression(Registers& r) override;
};

class AssignNode : public Branch {
public:
    ExpressionPtr expression;
    AssignNode(int line, int begin, int end) : Branch(line, begin, end) {}
    BranchPtr invert() override {
        auto n = std::make_shared<AssignNode>(line, end, begin);
        n->expression = expression;
        return n;
    }
    int getRegister() override { return -1; }
    ExpressionPtr asExpression(Registers& r) override {
        if (expression) return expression;
        Constant c;
        return std::make_shared<ConstantExpression>(c, -1);
    }
    void useExpression(ExpressionPtr expr) override { expression = expr; }
};

class TrueNode : public Branch {
public:
    int reg;
    bool invertFlag;
    TrueNode(int r, bool inv, int line, int begin, int end)
        : Branch(line, begin, end), reg(r), invertFlag(inv) {
        setTarget = r;
    }
    BranchPtr invert() override {
        return std::make_shared<TrueNode>(reg, !invertFlag, line, end, begin);
    }
    int getRegister() override { return reg; }
    ExpressionPtr asExpression(Registers& r) override {
        Constant c;
        c.type = 1;
        c.b = !invertFlag;
        return std::make_shared<ConstantExpression>(c, -1);
    }
};

ExpressionPtr TestNode::asExpression(Registers& r) {
    if (invertFlag) {
        return std::make_shared<NotBranch>(this->invert())->asExpression(r);
    }
    return r.getExpression(test, line);
}

ExpressionPtr TestSetNode::asExpression(Registers& r) {
    return r.getExpression(test, line);
}

ExpressionPtr EQNode::asExpression(Registers& r) {
    std::string opStr = invertFlag ? "~=" : "==";
    return std::make_shared<BinaryExpression>(opStr,
        r.getKExpression(left, line),
        r.getKExpression(right, line),
        Expression::PREC_COMPARE, Expression::ASSOC_LEFT);
}

ExpressionPtr LTNode::asExpression(Registers& r) {
    bool transpose = false;
    ExpressionPtr le = r.getKExpression(left, line);
    ExpressionPtr re = r.getKExpression(right, line);
    if (!r.f.isConstant(left) && !r.f.isConstant(right)) {
        transpose = r.getUpdated(left, line) > r.getUpdated(right, line);
    } else {
        transpose = re->getConstantIndex() < le->getConstantIndex();
    }
    std::string opStr = !transpose ? "<" : ">";
    ExpressionPtr a = transpose ? re : le;
    ExpressionPtr b = transpose ? le : re;
    ExpressionPtr res = std::make_shared<BinaryExpression>(opStr, a, b,
        Expression::PREC_COMPARE, Expression::ASSOC_LEFT);
    if (invertFlag) {
        res = std::make_shared<UnaryExpression>("not ", res, Expression::PREC_UNARY);
    }
    return res;
}

ExpressionPtr LENode::asExpression(Registers& r) {
    bool transpose = false;
    ExpressionPtr le = r.getKExpression(left, line);
    ExpressionPtr re = r.getKExpression(right, line);
    if (!r.f.isConstant(left) && !r.f.isConstant(right)) {
        transpose = r.getUpdated(left, line) > r.getUpdated(right, line);
    } else {
        transpose = re->getConstantIndex() < le->getConstantIndex();
    }
    std::string opStr = !transpose ? "<=" : ">=";
    ExpressionPtr a = transpose ? re : le;
    ExpressionPtr b = transpose ? le : re;
    ExpressionPtr res = std::make_shared<BinaryExpression>(opStr, a, b,
        Expression::PREC_COMPARE, Expression::ASSOC_LEFT);
    if (invertFlag) {
        res = std::make_shared<UnaryExpression>("not ", res, Expression::PREC_UNARY);
    }
    return res;
}

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

class Decompiler {
public:
    const LFunction& function;
    Function f;
    CodeView code;
    int regCount;
    int length;
    int params;
    int vararg;
    Op tforTarget;
    Op forTarget;
    std::vector<DeclarationPtr> declList;
    std::shared_ptr<Upvalues> upvalues;
    std::shared_ptr<Registers> r;
    std::shared_ptr<OuterBlock> outer;
    std::vector<BlockPtr> blocks;
    std::vector<bool> skip;
    std::vector<bool> reverseTarget;
    Stack<BranchPtr>* backup = nullptr;

    Decompiler(const LFunction& fn, const CodeExtract& cx, const OpcodeMap& om,
               const std::vector<DeclarationPtr>* parentDecls, int parentLine)
        : function(fn), f(fn)
    {
        code.f = &fn; code.ex = &cx; code.opcodes = &om;
        regCount = fn.maximumStackSize;
        if (regCount < 8) regCount = 8;
        length = (int)fn.code.size();
        params = fn.numParams;
        vararg = fn.vararg;
        tforTarget = Op::TFORLOOP;
        forTarget = Op::UNKNOWN;

        if (fn.stripped || fn.locals.empty()) {
            declList = findVariables(fn, cx, om, regCount, params);
        } else {
            for (auto& loc : fn.locals) {
                auto d = std::make_shared<Declaration>();
                d->name = loc.name;
                d->begin = loc.start;
                d->end = loc.end;
                declList.push_back(d);
            }
        }
        upvalues = std::make_shared<Upvalues>(fn, parentDecls, parentLine);
    }

    void decompile() {
        r = std::make_shared<Registers>(regCount, length, declList, f);
        findReverseTargetsImpl();
        handleBranches(true);
        auto outBlock = handleBranches(false);
        outer = std::dynamic_pointer_cast<OuterBlock>(outBlock);
        if (!outer) {
            outer = std::make_shared<OuterBlock>(&function, length);
        }
        processSequence(1, length);
    }

    void print(Output& out) {
        handleInitialDeclares(out);
        if (outer) outer->print(*this, out);
    }

    void handleInitialDeclares(Output& out) {
        std::vector<DeclarationPtr> initdecls;
        for (size_t i = (size_t)(params + (vararg & 1)); i < declList.size(); i++) {
            if (declList[i]->begin == 0 && !declList[i]->forLoop && !declList[i]->forLoopExplicit) {
                initdecls.push_back(declList[i]);
            }
        }
        if (!initdecls.empty()) {
            out.print("local ");
            out.print(initdecls[0]->name);
            for (size_t i = 1; i < initdecls.size(); i++) {
                out.print(", ");
                out.print(initdecls[i]->name);
            }
            out.println();
        }
    }

    void findReverseTargetsImpl() {
        reverseTarget.assign((size_t)(length + 1), false);
        for (int line = 1; line <= length; line++) {
            if (code.op(line) == Op::JMP && code.sBx(line) < 0) {
                int tgt = line + 1 + code.sBx(line);
                if (tgt >= 1 && tgt <= length) reverseTarget[(size_t)tgt] = true;
            }
        }
    }

    int fb2int(int fb) {
        int exponent = (fb >> 3) & 0x1f;
        if (exponent == 0) return fb;
        return ((fb & 7) + 8) << (exponent - 1);
    }

    std::vector<OperationPtr> processLine(int line);

    BlockPtr handleBranches(bool first);

    void processSequence(int begin, int end);

    bool isStatement(int line, int testRegister = -1);

    int getAssignment(int line);

    int breakTarget(int line) {
        int tline = INT32_MAX;
        for (auto& b : blocks) {
            if (b->breakable() && b->contains(line)) {
                if (b->end < tline) tline = b->end;
            }
        }
        return tline == INT32_MAX ? -1 : tline;
    }

    BlockPtr enclosingBlock(int line) {
        BlockPtr outer_ = blocks[0];
        BlockPtr enc = outer_;
        for (size_t i = 1; i < blocks.size(); i++) {
            auto next = blocks[i];
            if (next->isContainer() && enc->contains(next) && next->contains(line) && !next->loopRedirectAdjustment) {
                enc = next;
            }
        }
        return enc;
    }

    BlockPtr enclosingBreakableBlock(int line) {
        BlockPtr outer_ = blocks[0];
        BlockPtr enc = outer_;
        for (size_t i = 1; i < blocks.size(); i++) {
            auto next = blocks[i];
            if (enc->contains(next) && next->contains(line) && next->breakable() && !next->loopRedirectAdjustment) {
                enc = next;
            }
        }
        return enc == outer_ ? nullptr : enc;
    }

    BlockPtr enclosingUnprotectedBlock(int line) {
        BlockPtr outer_ = blocks[0];
        BlockPtr enc = outer_;
        for (size_t i = 1; i < blocks.size(); i++) {
            auto next = blocks[i];
            if (enc->contains(next) && next->contains(line) && next->isUnprotected() && !next->loopRedirectAdjustment) {
                enc = next;
            }
        }
        return enc == outer_ ? nullptr : enc;
    }

    BranchPtr popCondition(Stack<BranchPtr>& stack);
    BranchPtr popSetCondition(Stack<BranchPtr>& stack, int assignEnd, int target);
    BranchPtr popCompareSetCondition(Stack<BranchPtr>& stack, int assignEnd, int target);
    int adjustLine(int line, int target);
    BranchPtr helperPopSetCondition(Stack<BranchPtr>& stack, bool invert, int assignEnd, int target);

    bool isMoveIntoTarget(int line);
    TargetPtr getMoveIntoTargetTarget(int line, int previous);
    ExpressionPtr getMoveIntoTargetValue(int line, int previous);
    std::shared_ptr<Assignment> processOperation(OperationPtr op, int line, int nextLine, BlockPtr block);
};

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

void Decompiler::processSequence(int begin, int end) {
    int blockIndex = 1;
    Stack<BlockPtr> blockStack;
    blockStack.push(blocks[0]);
    skip.assign((size_t)(end + 2), false);
    for (int line = begin; line <= end; line++) {
        OperationPtr blockHandler = nullptr;
        while (blockStack.peek()->end <= line) {
            BlockPtr blk = blockStack.pop();
            blockHandler = blk->process(*this);
            if (blockHandler) break;
        }
        if (!blockHandler) {
            while (blockIndex < (int)blocks.size() && blocks[(size_t)blockIndex]->begin <= line) {
                blockStack.push(blocks[(size_t)blockIndex++]);
            }
        }
        BlockPtr block = blockStack.peek();
        r->startLine(line);
        if (skip[(size_t)line]) {
            auto newLocals = r->getNewLocals(line);
            if (!newLocals.empty()) {
                auto assign = std::make_shared<Assignment>();
                assign->declare(newLocals[0]->begin);
                for (auto& decl : newLocals) {
                    assign->addLast(std::make_shared<VariableTarget>(decl),
                                    r->getValue(decl->reg, line));
                }
                blockStack.peek()->addStatement(assign);
            }
            continue;
        }
        auto operations = processLine(line);
        auto newLocals = r->getNewLocals(blockHandler ? line - 1 : line);
        std::shared_ptr<Assignment> assign;
        if (!blockHandler) {
            if (code.op(line) == Op::LOADNIL) {
                assign = std::make_shared<Assignment>();
                int count = 0;
                for (auto& operation : operations) {
                    auto set = std::dynamic_pointer_cast<RegisterSet>(operation);
                    operation->process(*r, block);
                    if (r->isAssignable(set->reg, set->line)) {
                        assign->addLast(r->getTarget(set->reg, set->line), set->value);
                        count++;
                    }
                }
                if (count > 0) block->addStatement(assign);
            } else {
                for (auto& operation : operations) {
                    auto temp = processOperation(operation, line, line + 1, block);
                    if (temp) assign = temp;
                }
                if (assign && assign->getFirstValue() && assign->getFirstValue()->isMultiple()) {
                    block->addStatement(assign);
                }
            }
        } else {
            assign = processOperation(blockHandler, line, line, block);
        }
        if (assign) {
            if (!newLocals.empty()) {
                assign->declare(newLocals[0]->begin);
                for (auto& decl : newLocals) {
                    assign->addLast(std::make_shared<VariableTarget>(decl),
                                    r->getValue(decl->reg, line + 1));
                }
            }
        }
        if (!blockHandler) {
            if (!assign && !newLocals.empty() && code.op(line) != Op::FORPREP) {
                bool isTforSetup = false;
                if (code.op(line) == Op::JMP) {
                    int tgt = line + 1 + code.sBx(line);
                    if (tgt >= 1 && tgt <= length && code.op(tgt) == tforTarget) {
                        isTforSetup = true;
                    }
                }
                if (!isTforSetup) {
                    assign = std::make_shared<Assignment>();
                    assign->declare(newLocals[0]->begin);
                    for (auto& decl : newLocals) {
                        assign->addLast(std::make_shared<VariableTarget>(decl),
                                        r->getValue(decl->reg, line));
                    }
                    blockStack.peek()->addStatement(assign);
                }
            }
        }
        if (blockHandler) {
            line--;
            continue;
        }
    }
}

BlockPtr Decompiler::handleBranches(bool first) {
    std::vector<BlockPtr> oldBlocks = blocks;
    blocks.clear();
    auto outerBlock = std::make_shared<OuterBlock>(&function, length);
    blocks.push_back(outerBlock);
    std::vector<bool> isBreak((size_t)(length + 1), false);
    std::vector<bool> loopRemoved((size_t)(length + 1), false);
    if (!first) {
        for (auto& block : oldBlocks) {
            if (std::dynamic_pointer_cast<AlwaysLoop>(block)) blocks.push_back(block);
            auto br = std::dynamic_pointer_cast<Break>(block);
            if (br) {
                blocks.push_back(block);
                if (block->begin >= 0 && block->begin < (int)isBreak.size())
                    isBreak[(size_t)block->begin] = true;
            }
        }
        std::vector<BlockPtr> deleteList;
        for (auto& block : blocks) {
            if (std::dynamic_pointer_cast<AlwaysLoop>(block)) {
                for (auto& block2 : blocks) {
                    if (block != block2 && block->begin == block2->begin) {
                        if (block->end < block2->end) {
                            deleteList.push_back(block);
                            if (block->end - 1 >= 0 && block->end - 1 < (int)loopRemoved.size())
                                loopRemoved[(size_t)(block->end - 1)] = true;
                        } else {
                            deleteList.push_back(block2);
                            if (block2->end - 1 >= 0 && block2->end - 1 < (int)loopRemoved.size())
                                loopRemoved[(size_t)(block2->end - 1)] = true;
                        }
                    }
                }
            }
        }
        for (auto& b : deleteList) {
            blocks.erase(std::remove(blocks.begin(), blocks.end(), b), blocks.end());
        }
    }
    skip.assign((size_t)(length + 2), false);
    Stack<BranchPtr> stack;
    bool reduce = false;
    bool testset = false;
    int testsetend = -1;
    for (int line = 1; line <= length; line++) {
        if (!skip[(size_t)line]) {
            switch (code.op(line)) {
                case Op::EQ: {
                    auto node = std::make_shared<EQNode>(code.B(line), code.C(line),
                        code.A(line) != 0, line, line + 2, line + 2 + code.sBx(line + 1));
                    stack.push(node);
                    skip[(size_t)(line + 1)] = true;
                    if (node->end >= 1 && node->end <= length && code.op(node->end) == Op::LOADBOOL) {
                        if (code.C(node->end) != 0) {
                            node->isCompareSet = true;
                            node->setTarget = code.A(node->end);
                        } else if (node->end - 1 >= 1 && code.op(node->end - 1) == Op::LOADBOOL && code.C(node->end - 1) != 0) {
                            node->isCompareSet = true;
                            node->setTarget = code.A(node->end);
                        }
                    }
                    continue;
                }
                case Op::LT: {
                    auto node = std::make_shared<LTNode>(code.B(line), code.C(line),
                        code.A(line) != 0, line, line + 2, line + 2 + code.sBx(line + 1));
                    stack.push(node);
                    skip[(size_t)(line + 1)] = true;
                    if (node->end >= 1 && node->end <= length && code.op(node->end) == Op::LOADBOOL) {
                        if (code.C(node->end) != 0) {
                            node->isCompareSet = true;
                            node->setTarget = code.A(node->end);
                        } else if (node->end - 1 >= 1 && code.op(node->end - 1) == Op::LOADBOOL && code.C(node->end - 1) != 0) {
                            node->isCompareSet = true;
                            node->setTarget = code.A(node->end);
                        }
                    }
                    continue;
                }
                case Op::LE: {
                    auto node = std::make_shared<LENode>(code.B(line), code.C(line),
                        code.A(line) != 0, line, line + 2, line + 2 + code.sBx(line + 1));
                    stack.push(node);
                    skip[(size_t)(line + 1)] = true;
                    if (node->end >= 1 && node->end <= length && code.op(node->end) == Op::LOADBOOL) {
                        if (code.C(node->end) != 0) {
                            node->isCompareSet = true;
                            node->setTarget = code.A(node->end);
                        } else if (node->end - 1 >= 1 && code.op(node->end - 1) == Op::LOADBOOL && code.C(node->end - 1) != 0) {
                            node->isCompareSet = true;
                            node->setTarget = code.A(node->end);
                        }
                    }
                    continue;
                }
                case Op::TEST: {
                    stack.push(std::make_shared<TestNode>(code.A(line), code.C(line) != 0,
                        line, line + 2, line + 2 + code.sBx(line + 1)));
                    skip[(size_t)(line + 1)] = true;
                    continue;
                }
                case Op::TESTSET: {
                    skip[(size_t)(line + 1)] = true;
                    continue;
                }
                case Op::JMP: {
                    reduce = true;
                    int tline = line + 1 + code.sBx(line);
                    if (tline >= 2 && tline - 1 <= length && code.op(tline - 1) == Op::LOADBOOL && code.C(tline - 1) != 0) {
                        stack.push(std::make_shared<TrueNode>(code.A(tline - 1), false, line, line + 1, tline));
                        skip[(size_t)(line + 1)] = true;
                    } else if (tline >= 1 && tline <= length && code.op(tline) == tforTarget && !skip[(size_t)tline]) {
                        int A = code.A(tline);
                        int C = code.C(tline);
                        if (C >= 1) {
                            r->setInternalLoopVariable(A, tline, line + 1);
                            r->setInternalLoopVariable(A + 1, tline, line + 1);
                            r->setInternalLoopVariable(A + 2, tline, line + 1);
                            for (int idx = 1; idx <= C; idx++) {
                                std::string nm;
                                if (C == 2 && idx == 1) nm = "k";
                                else if (C == 2 && idx == 2) nm = "v";
                                else nm = "v" + std::to_string(idx - 1);
                                r->setExplicitLoopVariable(A + 2 + idx, line, tline + 2, nm);
                            }
                            skip[(size_t)tline] = true;
                            if (tline + 1 < (int)skip.size()) skip[(size_t)(tline + 1)] = true;
                            blocks.push_back(std::make_shared<TForBlock>(&function, line + 1, tline + 2, A, C, *r));
                        }
                    } else if (code.sBx(line) == 2 && line + 1 <= length && code.op(line + 1) == Op::LOADBOOL && code.C(line + 1) != 0) {
                        blocks.push_back(std::make_shared<BooleanIndicator>(&function, line));
                    } else if (tline >= 1 && tline <= length && code.op(tline) == Op::JMP && code.sBx(tline) + tline == line) {
                        if (first) blocks.push_back(std::make_shared<AlwaysLoop>(&function, line, tline + 1));
                        skip[(size_t)tline] = true;
                    } else {
                        if (first || (line >= 0 && line <= length && loopRemoved[(size_t)line]) ||
                            (line + 1 <= length && reverseTarget[(size_t)(line + 1)])) {
                            if (!isBreak[(size_t)line]) {
                                if (tline > line) {
                                    isBreak[(size_t)line] = true;
                                    blocks.push_back(std::make_shared<Break>(&function, line, tline));
                                } else {
                                    BlockPtr enclosing = enclosingBreakableBlock(line);
                                    if (enclosing && enclosing->breakable() && enclosing->end >= 1 &&
                                        enclosing->end <= length && code.op(enclosing->end) == Op::JMP &&
                                        code.sBx(enclosing->end) + enclosing->end + 1 == tline) {
                                        isBreak[(size_t)line] = true;
                                        blocks.push_back(std::make_shared<Break>(&function, line, enclosing->end));
                                    } else {
                                        blocks.push_back(std::make_shared<AlwaysLoop>(&function, tline, line + 1));
                                    }
                                }
                            }
                        }
                    }
                    break;
                }
                case Op::FORPREP: {
                    reduce = true;
                    int fpTgt = line + 1 + code.sBx(line);
                    int fpEnd = line + 2 + code.sBx(line);
                    blocks.push_back(std::make_shared<ForBlock>(&function, line + 1,
                        fpEnd, code.A(line), *r));
                    if (fpTgt >= 0 && fpTgt < (int)skip.size()) skip[(size_t)fpTgt] = true;
                    r->setInternalLoopVariable(code.A(line), line, fpEnd);
                    r->setInternalLoopVariable(code.A(line) + 1, line, fpEnd);
                    r->setInternalLoopVariable(code.A(line) + 2, line, fpEnd);
                    r->setExplicitLoopVariable(code.A(line) + 3, line, fpEnd, "i");
                    break;
                }
                case Op::FORLOOP:
                    break;
                default:
                    reduce = isStatement(line);
                    break;
            }
        }
        if ((line + 1) <= length && reverseTarget[(size_t)(line + 1)]) reduce = true;
        (void)testset; (void)testsetend;
        if (stack.empty()) reduce = false;
        if (reduce) {
            reduce = false;
            Stack<BranchPtr> conditions;
            Stack<Stack<BranchPtr>> backups;
            do {
                bool isAssignNode = std::dynamic_pointer_cast<TestSetNode>(stack.peek()) != nullptr;
                int assignEnd = stack.peek()->end;
                bool compareCorrect = false;
                if (std::dynamic_pointer_cast<TrueNode>(stack.peek())) {
                    isAssignNode = true;
                    compareCorrect = true;
                    if (assignEnd <= length && code.C(assignEnd) != 0) assignEnd += 2;
                    else assignEnd += 1;
                } else if (stack.peek()->isCompareSet) {
                    if (stack.peek()->begin > length || code.op(stack.peek()->begin) != Op::LOADBOOL || code.C(stack.peek()->begin) == 0) {
                        isAssignNode = true;
                        if (assignEnd <= length && code.C(assignEnd) != 0) assignEnd += 2;
                        else assignEnd += 1;
                        compareCorrect = true;
                    }
                }
                if (!compareCorrect && assignEnd - 1 == stack.peek()->begin && stack.peek()->begin <= length &&
                    code.op(stack.peek()->begin) == Op::LOADBOOL && code.C(stack.peek()->begin) != 0) {
                    backup = nullptr;
                    int beginX = stack.peek()->begin;
                    assignEnd = beginX + 2;
                    int target = code.A(beginX);
                    auto cond = popCompareSetCondition(stack, assignEnd, target);
                    cond->setTarget = target;
                    cond->end = assignEnd;
                    cond->begin = beginX;
                    conditions.push(cond);
                } else if (isAssignNode) {
                    backup = nullptr;
                    int target = stack.peek()->setTarget;
                    int beginX = stack.peek()->begin;
                    auto cond = popSetCondition(stack, assignEnd, target);
                    cond->setTarget = target;
                    cond->end = assignEnd;
                    cond->begin = beginX;
                    conditions.push(cond);
                } else {
                    static Stack<BranchPtr> bk;
                    bk = Stack<BranchPtr>();
                    backup = &bk;
                    conditions.push(popCondition(stack));
                    bk.reverse();
                }
                Stack<BranchPtr> bkcopy;
                if (backup) bkcopy = *backup;
                backups.push(bkcopy);
            } while (!stack.empty());
            do {
                BranchPtr cond = conditions.pop();
                auto bk = std::make_shared<Stack<BranchPtr>>(backups.pop());
                int brTarget = breakTarget(cond->begin);
                bool breakable = (brTarget >= 1);
                if (breakable && brTarget >= 1 && brTarget <= length && code.op(brTarget) == Op::JMP) {
                    brTarget += 1 + code.sBx(brTarget);
                }
                if (breakable && brTarget == cond->end) {
                    auto immediateEnclosing = enclosingBlock(cond->begin);
                    auto breakableEnclosing = enclosingBreakableBlock(cond->begin);
                    int loopstart = immediateEnclosing->end;
                    if (immediateEnclosing == breakableEnclosing) loopstart--;
                    for (int iline = loopstart; iline >= std::max(cond->begin, immediateEnclosing->begin); iline--) {
                        if (iline >= 1 && iline <= length && code.op(iline) == Op::JMP && iline + 1 + code.sBx(iline) == brTarget) {
                            cond->end = iline;
                            break;
                        }
                    }
                }
                bool hasTail = cond->end >= 2 && cond->end - 1 <= length && code.op(cond->end - 1) == Op::JMP;
                int tail = hasTail ? cond->end + code.sBx(cond->end - 1) : -1;
                int originalTail = tail;
                BlockPtr enclosing = enclosingUnprotectedBlock(cond->begin);
                if (enclosing) {
                    if (enclosing->getLoopback() == cond->end) {
                        cond->end = enclosing->end - 1;
                        hasTail = cond->end >= 2 && cond->end - 1 <= length && code.op(cond->end - 1) == Op::JMP;
                        tail = hasTail ? cond->end + code.sBx(cond->end - 1) : -1;
                    }
                    if (hasTail && enclosing->getLoopback() == tail) {
                        tail = enclosing->end - 1;
                    }
                }
                if (cond->isSet) {
                    blocks.push_back(std::make_shared<IfThenEndBlock>(&function, cond, bk, *r));
                } else if (cond->begin >= 1 && cond->begin <= length && code.op(cond->begin) == Op::LOADBOOL && code.C(cond->begin) != 0) {
                    int beginX = cond->begin;
                    if (code.B(beginX) == 0) cond = cond->invert();
                    blocks.push_back(std::make_shared<IfThenEndBlock>(&function, cond, bk, *r));
                } else if (cond->end < cond->begin) {
                    if (cond->end - 1 >= 0 && cond->end - 1 <= length && isBreak[(size_t)(cond->end - 1)]) {
                        skip[(size_t)(cond->end - 1)] = true;
                        blocks.push_back(std::make_shared<WhileBlock>(&function, cond->invert(), originalTail, *r));
                    } else {
                        blocks.push_back(std::make_shared<RepeatBlock>(&function, cond, *r));
                    }
                } else if (hasTail) {
                    Op endOp = (cond->end - 2 >= 1 && cond->end - 2 <= length) ? code.op(cond->end - 2) : Op::UNKNOWN;
                    bool isEndCondJump = endOp == Op::EQ || endOp == Op::LE || endOp == Op::LT ||
                                          endOp == Op::TEST || endOp == Op::TESTSET || endOp == Op::TEST50;
                    if (tail > cond->end || (tail == cond->end && !isEndCondJump)) {
                        if (cond->end - 1 >= 1 && cond->end - 1 <= length) {
                            skip[(size_t)(cond->end - 1)] = true;
                        }
                        bool emptyElse = (tail == cond->end);
                        auto ifthen = std::make_shared<IfThenElseBlock>(&function, cond, originalTail, emptyElse, *r);
                        blocks.push_back(ifthen);
                        if (!emptyElse) {
                            blocks.push_back(std::make_shared<ElseEndBlock>(&function, cond->end, tail));
                        }
                    } else {
                        int loopback = tail;
                        bool existsStatement = false;
                        for (int sl = loopback; sl < cond->begin; sl++) {
                            if (sl >= 1 && sl <= length && !skip[(size_t)sl] && isStatement(sl)) {
                                existsStatement = true;
                                break;
                            }
                        }
                        if (loopback >= cond->begin || existsStatement) {
                            blocks.push_back(std::make_shared<IfThenEndBlock>(&function, cond, bk, *r));
                        } else {
                            if (cond->end - 1 >= 1 && cond->end - 1 <= length) {
                                skip[(size_t)(cond->end - 1)] = true;
                            }
                            blocks.push_back(std::make_shared<WhileBlock>(&function, cond, originalTail, *r));
                        }
                    }
                } else {
                    blocks.push_back(std::make_shared<IfThenEndBlock>(&function, cond, bk, *r));
                }
            } while (!conditions.empty());
        }
    }
    for (auto& decl : declList) {
        if (!decl->forLoop && !decl->forLoopExplicit) {
            bool needsDoEnd = true;
            for (auto& block : blocks) {
                if (block->contains(decl->begin)) {
                    if (block->scopeEnd() == decl->end) {
                        needsDoEnd = false;
                        break;
                    }
                }
            }
            if (needsDoEnd) {
                blocks.push_back(std::make_shared<DoEndBlock>(&function, decl->begin, decl->end + 1));
            }
        }
    }
    blocks.erase(std::remove_if(blocks.begin(), blocks.end(), [&](BlockPtr b) {
        return b->begin >= 0 && b->begin <= length && skip[(size_t)b->begin] && std::dynamic_pointer_cast<Break>(b);
    }), blocks.end());
    std::sort(blocks.begin(), blocks.end(), [](BlockPtr a, BlockPtr b) {
        return compareBlocks(a, b) < 0;
    });
    backup = nullptr;
    return outerBlock;
}

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

}

extern std::string run_full_decompiler(const LFunction& main,
                                       const CodeExtract& ex,
                                       const OpcodeMap& opcodes);

std::string run_full_decompiler(const LFunction& main,
                                const CodeExtract& ex,
                                const OpcodeMap& opcodes) {
    try {
        Decompiler dc(main, ex, opcodes, nullptr, 0);
        dc.decompile();
        Output out;
        dc.print(out);
        std::string result = out.str();
        if (result.empty()) result = "-- (decompiled to empty body)\n";
        return result;
    } catch (const std::exception& e) {
        return std::string("-- decompiler exception: ") + e.what() + "\n";
    } catch (...) {
        return "-- decompiler exception: unknown\n";
    }
}

}
