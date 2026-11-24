// SPDX-License-Identifier: GPL-3.0-only

#include "paths.hpp"

#include "util.hpp"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <cstring>

namespace sdlagent::paths
{
namespace
{

std::filesystem::path gPref;
std::filesystem::path gData;
std::filesystem::path gSessions;
std::filesystem::path gConfig;
std::filesystem::path gSysPrompt;
std::filesystem::path gToolPrompt;
std::filesystem::path gTitlePrompt;
std::filesystem::path gGlobalAgents;
std::filesystem::path gCatalog;
std::filesystem::path gAttachments;
std::filesystem::path gGateways;
std::filesystem::path gWorkspace;
bool gInit = false;

void ensureInit()
{
    if (gInit)
        return;
    gInit = true;

    char* p = SDL_GetPrefPath(nullptr, "sdlagent");
    if (p)
    {
        gPref = std::filesystem::path(p);
        SDL_free(p);
    }
    else
    {
        gPref = std::filesystem::current_path() / "sdlagent-data";
    }

    std::error_code ec;
    std::filesystem::create_directories(gPref, ec);

    gSessions = gPref / "sessions";
    std::filesystem::create_directories(gSessions, ec);
    gData = gPref;

    gConfig = gPref / "config.json";
    gSysPrompt = gData / "sys_prompt.md";
    gToolPrompt = gData / "tool_prompt.md";
    gTitlePrompt = gData / "title_prompt.md";
    gCatalog = gData / "api.json";
    gAttachments = gData / "attachments.json";
    gGateways = gData / "gateways.json";
    const char* platform = SDL_GetPlatform();
    if (platform && std::strcmp(platform, "Android") == 0)
    {
        //The app's cwd is "/" on Android; keep tool paths inside app storage.
        gWorkspace = gPref / "workspace";
        std::error_code ec;
        std::filesystem::create_directories(gWorkspace, ec);
    }
    else
    {
        gWorkspace = std::filesystem::current_path();
    }
}

} //namespace

const std::filesystem::path& prefDir()
{
    ensureInit();
    return gPref;
}

const std::filesystem::path& defaultSessionsDir()
{
    ensureInit();
    return gSessions;
}

const std::filesystem::path& configFile()
{
    ensureInit();
    return gConfig;
}

const std::filesystem::path& dataDir()
{
    ensureInit();
    return gData;
}

void setDataDir(const std::filesystem::path& dir)
{
    ensureInit();

    std::error_code ec;
    std::filesystem::path abs = std::filesystem::absolute(dir, ec);
    if (ec)
        abs = dir;
    gData = abs.lexically_normal();
    gSysPrompt = gData / "sys_prompt.md";
    gToolPrompt = gData / "tool_prompt.md";
    gTitlePrompt = gData / "title_prompt.md";
    gGlobalAgents = gData / "AGENTS.md";
    gCatalog = gData / "api.json";
    gAttachments = gData / "attachments.json";
    gGateways = gData / "gateways.json";

    std::filesystem::create_directories(gData, ec);
}

const std::filesystem::path& sysPromptFile()
{
    ensureInit();
    return gSysPrompt;
}

const std::filesystem::path& toolPromptFile()
{
    ensureInit();
    return gToolPrompt;
}

const std::filesystem::path& titlePromptFile()
{
    ensureInit();
    return gTitlePrompt;
}

const std::filesystem::path& globalAgentsFile()
{
    ensureInit();
    return gGlobalAgents;
}

const std::filesystem::path& apiCatalogFile()
{
    ensureInit();
    return gCatalog;
}

const std::filesystem::path& attachmentsConfigFile()
{
    ensureInit();
    return gAttachments;
}

const std::filesystem::path& gatewaysFile()
{
    ensureInit();
    return gGateways;
}

const std::filesystem::path& defaultWorkspace()
{
    ensureInit();
    return gWorkspace;
}

std::string toUtf8(const std::filesystem::path& p)
{
#if defined(__cpp_lib_char8_t)
    std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
#else
    return p.u8string();
#endif
}

std::filesystem::path fromUtf8(const std::string& s)
{
#if defined(__cpp_lib_char8_t)
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
#else
    return std::filesystem::u8path(s);
#endif
}

std::string homeDir()
{
#ifdef _WIN32
    if (const char* p = std::getenv("USERPROFILE"))
        return p;
    const char* drive = std::getenv("HOMEDRIVE");
    const char* path = std::getenv("HOMEPATH");
    if (drive && path)
        return std::string(drive) + path;
#else
    if (const char* p = std::getenv("HOME"))
        return p;
#endif
    return {};
}

bool isMobile()
{
    const char* platform = SDL_GetPlatform();
    if (!platform)
        return false;
    return std::strcmp(platform, "Android") == 0 ||
           std::strcmp(platform, "iOS") == 0;
}

std::string expandUser(std::string s)
{
    if (s == "~")
        return homeDir();

    if (s.size() >= 2 && s[0] == '~' && (s[1] == '/' || s[1] == '\\'))
    {
        std::string h = homeDir();
        if (!h.empty())
            return h + s.substr(1);
    }
    return s;
}

std::filesystem::path resolveDir(const std::string& value,
                                 const std::filesystem::path& fallback)
{
    std::string v = trim(expandUser(value));
    if (v.empty())
        return std::filesystem::absolute(fallback);

    std::filesystem::path p = fromUtf8(v);
    if (p.is_relative())
        p = std::filesystem::current_path() / p;

    std::error_code ec;
    std::filesystem::path abs = std::filesystem::absolute(p, ec);
    return ec ? p : abs.lexically_normal();
}

} //namespace sdlagent::paths
