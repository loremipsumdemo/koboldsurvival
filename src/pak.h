#pragma once
#include <cstddef>
#include <string>
#include <vector>

bool pakOpen(const std::string& pakPath);

bool pakFind(const std::string& name, const unsigned char*& bytes, size_t& size);

bool pakHas(const std::string& name);

std::vector<std::string> pakNames(const std::string& prefix, const std::string& suffix);
