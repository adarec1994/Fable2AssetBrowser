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

