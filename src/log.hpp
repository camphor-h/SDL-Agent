// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace sdlagent::log
{

//Append one line to the in-memory log and mirror it to stderr.
void writeLine(const std::string& line);
void writef(const char* fmt, ...);

//All retained lines, oldest first, one per line.
std::string dump();

//Increments whenever a line is appended; lets the UI detect changes cheaply.
uint64_t revision();

bool exportTo(const std::filesystem::path& path, std::string& err);

} //namespace sdlagent::log
