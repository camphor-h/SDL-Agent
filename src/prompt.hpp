// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <filesystem>
#include <string>

namespace sdlagent::prompt
{

//Built-in prompt files, written on first launch.
const char* defaultSysPrompt();
const char* defaultToolPrompt();
const char* defaultTitlePrompt();

//Create sys_prompt.md / tool_prompt.md / title_prompt.md when missing.
void ensureFiles();

std::string readFile(const std::filesystem::path& file, const char* fallback);
bool writeFile(const std::filesystem::path& file, const std::string& text,
               std::string& err);

//Build the final system message: user sys prompt + optional tool prompt +
//merged AGENTS.md, then a runtime block. `startedAtMs` is frozen at the first
//send of the conversation so the prefix never changes.
std::string compose(const std::string& sysText, const std::string& toolText,
                    bool toolsEnabled, const std::string& os,
                    const std::string& workspace, const std::string& model,
                    const std::string& agentsText, uint64_t startedAtMs);

} //namespace sdlagent::prompt
