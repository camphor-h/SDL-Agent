// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "i18n.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace sdlagent
{

enum class ToolMode
{
    Off,
    Ask,
    Auto
};

struct ApiProfile
{
    std::string id;
    std::string name;
    std::string baseUrl;
    std::string apiKey;
    std::string model;
    std::string reasoningEffort; //empty = provider default
    std::string apiProtocol = "openai"; //openai | responses | anthropic
    double      temperature = 0.0;
    int         contextTokens = 1048576;
    int         maxTokens = 524288;
};

struct Config
{
    std::string baseUrl;
    std::string apiKey;
    std::string model;
    std::string reasoningEffort; //empty = provider default
    std::string apiProtocol = "openai"; //openai | responses | anthropic

    double temperature = 0.0;
    int    contextTokens = 1048576;
    int    maxTokens   = 524288;

    //saved API configurations
    std::vector<ApiProfile> profiles;
    std::string             activeProfileId;

    //directory for prompt files and api.json; empty = default pref dir
    //config.json itself always stays in the fixed pref dir
    std::string dataDir;

    //effective sessions directory (absolute); filled by the app on load
    std::string sessionsDir;

    ToolMode toolMode = ToolMode::Ask;
    bool     showReasoning = false;
    bool     autoTitle = true;

    std::string theme = "light"; //dark | light
    Lang        lang  = Lang::Zh;
    float       uiScale = 1.0f;

    //font backend: "ttf" (system font; built-in bitmap font is disabled)
    std::string fontBackend = "ttf";
    std::string fontPath;      //empty = auto-detect a system font (SimHei)
    int         fontPointSize = 16;

    bool onboarded = false;

    bool apiReady() const;
};

Config loadConfig(const std::filesystem::path& file);
bool   saveConfig(const std::filesystem::path& file, const Config& cfg,
                  std::string& err);

const char* toolModeToString(ToolMode m);
ToolMode    toolModeFromString(const std::string& s);

} //namespace sdlagent
