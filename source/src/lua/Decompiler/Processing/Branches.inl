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

