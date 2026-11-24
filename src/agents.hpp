// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <filesystem>
#include <string>

namespace sdlagent::agents
{

//Merged global + project AGENTS.md, with a stable hash for change detection.
struct Snapshot
{
    std::string text;
    std::string hash;
};

//Global file is read first, the project file second. Missing files are
//skipped; when both exist, the project rules are declared to take precedence.
Snapshot load(const std::filesystem::path& globalFile,
              const std::filesystem::path& projectFile);

} //namespace sdlagent::agents
