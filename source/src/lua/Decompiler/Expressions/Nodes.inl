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

