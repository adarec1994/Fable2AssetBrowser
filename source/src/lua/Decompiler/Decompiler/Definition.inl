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

