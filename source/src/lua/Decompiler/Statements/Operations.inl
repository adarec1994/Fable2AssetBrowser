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

