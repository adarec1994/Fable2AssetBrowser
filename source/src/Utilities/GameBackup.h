#pragma once

#include <string>





namespace GameBackup {

bool Exists();
bool Busy();
std::string StatusText();   



bool RequireBackup(std::string& error);

void CreateAsync();
void RestoreAsync();




void DrawMainMenu();

}
