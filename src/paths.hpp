// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <filesystem>
#include <string>

namespace sdlagent::paths
{

//SDL_GetPrefPath(), with a sane fallback next to the executable
const std::filesystem::path& prefDir();

const std::filesystem::path& defaultSessionsDir();
const std::filesystem::path& configFile();

//Directory for sys_prompt.md, tool_prompt.md and api.json.
//Defaults to prefDir; can be changed at runtime from settings.
const std::filesystem::path& dataDir();
void setDataDir(const std::filesystem::path& dir);

const std::filesystem::path& sysPromptFile();
const std::filesystem::path& toolPromptFile();
const std::filesystem::path& titlePromptFile();
const std::filesystem::path& globalAgentsFile();
const std::filesystem::path& apiCatalogFile();
const std::filesystem::path& attachmentsConfigFile();
const std::filesystem::path& gatewaysFile();
const std::filesystem::path& defaultWorkspace();

//UTF-8 <-> filesystem::path (Windows uses UTF-16 internally)
std::string toUtf8(const std::filesystem::path& p);
std::filesystem::path fromUtf8(const std::string& s);

//expand a leading "~" to the user's home directory
std::string expandUser(std::string s);

//resolve a user-entered directory; empty string -> fallback
std::filesystem::path resolveDir(const std::string& value,
                                 const std::filesystem::path& fallback);

std::string homeDir();

//Android / iOS: paths are app-sandbox defaults and must not be user-editable.
bool isMobile();

} //namespace sdlagent::paths
