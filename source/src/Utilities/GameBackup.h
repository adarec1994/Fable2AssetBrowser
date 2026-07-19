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

// True when the path points inside the pristine-backup copy
// (f2ab_backup); editing tools must never write there.
bool IsBackupPath(const std::string& path);

void CreateAsync();
void RestoreAsync();




void DrawMainMenu();

}
