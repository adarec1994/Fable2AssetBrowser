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

