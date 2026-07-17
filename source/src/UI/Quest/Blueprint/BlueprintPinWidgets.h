#pragma once

#include "Quest/Blueprint/BlueprintGraph.h"

namespace BlueprintUIDetail {



bool DrawItemPicker(Quest::Bp::Pin& pin);
bool DrawLevelPicker(Quest::Bp::Pin& pin);
bool DrawEntityDefPicker(Quest::Bp::Pin& pin);


bool DrawEntityEditor(const Quest::Bp::BlueprintQuest& quest,
                      Quest::Bp::Pin& pin);

}
