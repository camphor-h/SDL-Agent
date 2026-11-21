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

//Build the final system message: user sys prompt + optional tool prompt,
//then a runtime block (OS/CPU/memory/workspace/time/model) at the end.
std::string compose(const std::string& sysText, const std::string& toolText,
                    bool toolsEnabled, const std::string& os,
                    const std::string& workspace, const std::string& model);

} //namespace sdlagent::prompt
