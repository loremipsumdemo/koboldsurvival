#pragma once
#include <string>
#include <vector>

// What the data tables say about each other that no one table can check: items named by mobs, props and other items
// exist, use scripts parse and each mob's faction is listed. Run after loadGameTables. Empty means all is well.
std::vector<std::string> gameDataProblems();
