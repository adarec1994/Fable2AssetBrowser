#pragma once
#include <string>

void progress_open(int total, const std::string &title);
void progress_update(int current, int total, const std::string &fname);
void progress_done();
void show_error_box(const std::string &msg);
void show_completion_box(const std::string &msg);