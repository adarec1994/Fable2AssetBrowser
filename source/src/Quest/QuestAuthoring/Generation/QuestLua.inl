std::string GenerateQuestLua(const AuthoredQuest& quest) {
    std::string validation_error;
    if (!ValidateSimpleQuest(quest, validation_error)) {
        return "-- Quest graph incomplete: " + validation_error + "\n";
    }
    if (is_childhood_skip_graph(quest)) {
        return generate_childhood_skip_patch_lua(quest);
    }
    const AuthoredNode& approach = *find_kind(quest, AuthoredNodeKind::ApproachNpc);
    const AuthoredNode& dialogue = *find_kind(quest, AuthoredNodeKind::Dialogue);
    const AuthoredNode& accept = *find_kind(quest, AuthoredNodeKind::AcceptQuest);
    const AuthoredNode& obtain = *find_kind(quest, AuthoredNodeKind::ObtainItem);
    const AuthoredNode& returning = *find_kind(quest, AuthoredNodeKind::ReturnToNpc);

    const std::string dialogue_tag = node_tag(quest, dialogue);
    const std::string accept_tag = node_tag(quest, accept);
    const std::string obtain_tag = node_tag(quest, obtain);
    const std::string return_tag = node_tag(quest, returning);
    const bool same_return_actor =
        approach.entity.entity_name == returning.entity.entity_name;

    std::ostringstream lua;
    lua << "module(..., package.seeall)\n"
        << "QuestManager.NewQuestThread(\"" << quest.quest_id << "\")\n\n"
        << "function " << quest.quest_id << ":Init()\n"
        << "  self.MissionSucceeded = false\n"
        << "  self.MissionFailed = false\n"
        << "  self.MissionStarted = false\n"
        << "  self.HasRequiredItem = false\n"
        << "  self.ItemBreadcrumbSet = false\n"
        << "  self.QuestName = \"" << quest.quest_id << "\"\n"
        << "  QuestTracker.Register(QuestManager.HeroEntity, self.QuestName, "
        << lua_quote("Quest_" + quest.quest_id) << ")\n"
        << "  QuestTracker.Unlock(QuestManager.HeroEntity, self.QuestName)\n"
        << "end\n\n"
        << "function " << quest.quest_id << ":BeginObjective()\n"
        << "  if self.MissionStarted then return end\n"
        << "  self.MissionStarted = true\n"
        << "  QuestTracker.SetAsActive(QuestManager.HeroEntity, self.QuestName, true)\n"
        << "  QuestTracker.SetAsPrimary(QuestManager.HeroEntity, self.QuestName)\n"
        << "  QuestTracker.SetObjectiveTag(QuestManager.HeroEntity, self.QuestName, "
        << lua_quote(obtain_tag) << ")\n"
        << "  QuestTracker.SetObjectiveLevel(QuestManager.HeroEntity, self.QuestName, "
        << lua_quote(obtain.item.source.level_id) << ")\n"
        << "end\n\n"
        << "function " << quest.quest_id << ":Update()\n"
        << "  while not self.MissionSucceeded and not self.MissionFailed do\n"
        << "    if self.MissionStarted and not self.HasRequiredItem then\n"
        << "      if not self.ItemBreadcrumbSet and IsLevelLoaded("
        << lua_quote(obtain.item.source.level_id) << ") then\n"
        << "        local source = self:GetEntityWithName("
        << lua_quote(obtain.item.source.entity_name) << ", \"object\")\n"
        << "        if source and source:IsAlive() then\n"
        << "          self.RequiredItemSource = source\n"
        << "          QuestTracker.SetObjectiveEntity(QuestManager.HeroEntity, self.QuestName, source, true)\n"
        << "          self.ItemBreadcrumbSet = true\n"
        << "        end\n"
        << "      end\n"
        << "      if Inventory.GetNumberOfItemsOfType(QuestManager.HeroEntity, "
        << lua_quote(obtain.item.internal_name) << ") >= " << obtain.item_count
        << " then\n"
        << "        self.HasRequiredItem = true\n"
        << "        if self.RequiredItemSource and self.RequiredItemSource:IsAlive() then\n"
        << "          QuestTracker.SetObjectiveEntity(QuestManager.HeroEntity, self.QuestName, self.RequiredItemSource, false)\n"
        << "        end\n"
        << "        QuestTracker.SetObjectiveTag(QuestManager.HeroEntity, self.QuestName, "
        << lua_quote(return_tag) << ", " << lua_quote(obtain_tag) << ")\n"
        << "        QuestTracker.SetObjectiveLevel(QuestManager.HeroEntity, self.QuestName, "
        << lua_quote(returning.entity.level_id) << ")\n"
        << "      end\n"
        << "    end\n"
        << "    coroutine.yield()\n"
        << "  end\n"
        << "  if self.MissionSucceeded then\n"
        << "    QuestTracker.ClearAllObjectiveEntities(QuestManager.HeroEntity, self.QuestName)\n"
        << "    QuestTracker.SetObjectiveAsCompleted(QuestManager.HeroEntity, self.QuestName, "
        << lua_quote(return_tag) << ")\n";
    for (const AuthoredReward& reward : quest.rewards) {
        switch (reward.kind) {
            case QuestRewardKind::Gold:
                if (reward.amount != 0) {
                    lua << "    Money.Modify(QuestManager.HeroEntity, "
                        << reward.amount << ")\n";
                }
                break;
            case QuestRewardKind::Renown:
                if (reward.amount != 0) {
                    lua << "    Stats.ModifyRenown(QuestManager.HeroEntity, "
                        << reward.amount << ")\n";
                }
                break;
            case QuestRewardKind::GeneralExperience:
            case QuestRewardKind::StrengthExperience:
            case QuestRewardKind::SkillExperience:
            case QuestRewardKind::WillExperience: {
                if (reward.amount == 0) break;
                const char* type =
                    reward.kind == QuestRewardKind::GeneralExperience
                        ? "EExperienceType.EXPERIENCE_GENERAL"
                    : reward.kind == QuestRewardKind::StrengthExperience
                        ? "EExperienceType.EXPERIENCE_STRENGTH"
                    : reward.kind == QuestRewardKind::SkillExperience
                        ? "EExperienceType.EXPERIENCE_SKILL"
                        : "EExperienceType.EXPERIENCE_WILL";
                lua << "    Experience.Modify(QuestManager.HeroEntity, "
                    << type << ", " << reward.amount << ", false)\n";
                break;
            }
            case QuestRewardKind::Item:
                lua << "    for reward_index = 1, " << reward.item_count
                    << " do\n"
                    << "      local reward_item = Inventory.AddItemOfType(QuestManager.HeroEntity, "
                    << lua_quote(reward.item.internal_name) << ")\n"
                    << "      GUI.DisplayReceivedItem(reward_item)\n"
                    << "    end\n";
                break;
            case QuestRewardKind::Morality:
                if (reward.amount != 0) {
                    lua << "    Stats.ModifyMorality(QuestManager.HeroEntity, "
                        << reward.amount << ")\n";
                }
                break;
            case QuestRewardKind::Purity:
                if (reward.amount != 0) {
                    lua << "    Stats.ModifyPurity(QuestManager.HeroEntity, "
                        << reward.amount << ")\n";
                }
                break;
        }
    }
    lua << "    QuestTracker.SetAsCompleted(QuestManager.HeroEntity, self.QuestName, true, true)\n"
        << "  else\n"
        << "    QuestTracker.SetAsFailed(QuestManager.HeroEntity, self.QuestName)\n"
        << "  end\n"
        << "end\n\n";

    auto emit_return_logic = [&](std::ostringstream& out) {
        out << "    if self.ParentQuest.HasRequiredItem then\n"
            << "      if self.CurrentState == 0 then\n"
            << "        QuestTracker.SetObjectiveEntity(QuestManager.HeroEntity, self.ParentQuest.QuestName, self.Entity, true)\n"
            << "        self.CurrentState = 1\n"
            << "      end\n"
            << "      if self.Interacted then\n"
            << "        QuestTracker.SetObjectiveEntity(QuestManager.HeroEntity, self.ParentQuest.QuestName, self.Entity, false)\n";
        if (returning.remove_item) {
            out << "        Inventory.RemoveAllItemsOfType(QuestManager.HeroEntity, "
                << lua_quote(returning.item.internal_name) << ")\n";
        }
        out << "        self.ParentQuest.MissionSucceeded = true\n"
            << "        self.CurrentState = 2\n"
            << "      end\n"
            << "    end\n";
    };

    lua << "QuestManager.NewEntityThread("
        << lua_quote(approach.entity.entity_name) << ")\n\n"
        << "function " << approach.entity.entity_name << ":Init()\n"
        << "  self.CurrentState = 0\n"
        << "  self.OfferInProgress = false\n"
        << "end\n\n"
        << "function " << approach.entity.entity_name << ":CustomUpdate()\n"
        << "  QuestTracker.SetQuestGiver(QuestManager.HeroEntity, self.ParentQuest.QuestName, self.Entity)\n"
        << "  while true do\n"
        << "    if not self.ParentQuest.MissionStarted and "
        << "IsDistanceBetweenThingsUnder(self.Entity, QuestManager.HeroEntity, "
        << approach.approach_radius << ") and not self.OfferInProgress then\n"
        << "      self.OfferInProgress = true\n";
    if (dialogue.entity.entity_name == approach.entity.entity_name) {
        lua << "      ScriptFunction.SaySimLine(self.Entity, "
            << lua_quote(dialogue_tag) << ")\n";
    } else {
        lua << "      local speaker = self:GetEntityWithName("
            << lua_quote(dialogue.entity.entity_name) << ", \"creature\")\n"
            << "      if speaker and speaker:IsAlive() then\n"
            << "        ScriptFunction.SaySimLine(speaker, "
            << lua_quote(dialogue_tag) << ")\n"
            << "      end\n";
    }
    lua << "      local accepted = self:ShowToasterAcceptBoxWithDialogueUntilCondition(\n"
        << "        " << lua_quote(accept_tag) << ", "
        << lua_quote("Quest_" + quest.quest_id) << ",\n"
        << "        {QuestName = self.ParentQuest.QuestName})\n"
        << "      if accepted then\n"
        << "        self.ParentQuest:BeginObjective()\n"
        << "      end\n"
        << "      while IsDistanceBetweenThingsUnder(self.Entity, QuestManager.HeroEntity, "
        << approach.approach_radius << ") do coroutine.yield() end\n"
        << "      self.OfferInProgress = false\n"
        << "    end\n";
    if (same_return_actor) emit_return_logic(lua);
    lua << "    coroutine.yield()\n"
        << "  end\n"
        << "end\n";

    if (!same_return_actor) {
        lua << "\nQuestManager.NewEntityThread("
            << lua_quote(returning.entity.entity_name) << ")\n\n"
            << "function " << returning.entity.entity_name << ":Init()\n"
            << "  self.CurrentState = 0\n"
            << "end\n\n"
            << "function " << returning.entity.entity_name << ":CustomUpdate()\n"
            << "  while true do\n";
        emit_return_logic(lua);
        lua << "    coroutine.yield()\n"
            << "  end\n"
            << "end\n";
    }
    return lua.str();
}
