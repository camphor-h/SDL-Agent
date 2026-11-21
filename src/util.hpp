// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstdint>
#include <string>

namespace sdlagent
{
//[utilities shared by every module]

uint64_t nowMs();

//short unique id: <milliseconds>-<random hex>
std::string makeId();

std::string trim(const std::string& s);
std::string toLower(std::string s);

bool startsWith(const std::string& s, const std::string& prefix);
bool endsWith(const std::string& s, const std::string& suffix);

//number of Unicode codepoints (invalid bytes count as one)
size_t utf8Length(const std::string& s);

//first maxCp codepoints; never cuts a multi-byte sequence
std::string utf8Prefix(const std::string& s, size_t maxCp);

//replace every occurrence of `from` with `to`
std::string replaceAll(std::string s, const std::string& from, const std::string& to);

std::string formatBytes(uint64_t bytes);

//single-line preview used for session titles / confirmations
std::string firstLine(const std::string& s, size_t maxCp = 40);

} //namespace sdlagent
