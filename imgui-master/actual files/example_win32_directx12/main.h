#pragma once
#include <vector>
#include <string>
#include <ctime>
#include "imgui.h"

struct category {
    char name[50];
    int priority = 0;
    ImVec4 color;
};


struct task {
    char name[50];
    int priority;
    bool completed = false;
    tm dueDate = {};
    category cat;
};


#define SECINDAY 86400
#define SECINHOUR 3600

void saveFile(std::vector <task>& list, std::vector<category>& categories, std::vector <task> &completedList, const char* filename);
void openFile(std::vector <task>& list, std::vector<category>& categories, std::vector <task>& completedList, const char* filename);
void newfile(std::vector<task>& list, std::vector<category>& categories, std::vector <task>& completedList, const char* filename);
std::string GetPathFromUser(bool saveMode);
const char* getSavePath();
void saveConfig(const char* lastPath);
void loadConfig(char* lastPath, int size);
int getOccurrences(tm dueDate, tm endDate, int repeatInt, int idx);
