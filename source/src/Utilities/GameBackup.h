#pragma once

#include <string>
#include <vector>





namespace GameBackup {

bool Exists();
bool Busy();
std::string StatusText();   



bool RequireBackup(std::string& error);
bool EnsureFilesCovered(const std::vector<std::string>& paths,
                        std::string& error);

void CreateAsync();
void RestoreAsync();




void DrawMainMenu();

}
