// SPDX-License-Identifier: GPL-3.0-only

#include "prompt.hpp"

#include "i18n.hpp"
#include "paths.hpp"
#include "util.hpp"
#include "version.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <system_error>

//Embedded default prompts generated from assets/prompts/*.md.
#include "default_prompts.inc"

namespace sdlagent::prompt
{
namespace
{

std::string dateOf(uint64_t ms)
{
    if (ms == 0)
        return "未知";

    std::time_t t = (std::time_t)(ms / 1000);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    return buf;
}

std::string runtimeBlock(const std::string& os, const std::string& workspace,
                         const std::string& model, uint64_t startedAtMs)
{
    int cores = SDL_GetNumLogicalCPUCores();
    int ramMb = SDL_GetSystemRAM();

    char mem[32];
    if (ramMb > 0)
        std::snprintf(mem, sizeof(mem), "%.1f GB", ramMb / 1024.0);
    else
        std::snprintf(mem, sizeof(mem), "未知");

    std::ostringstream out;
    out << "[运行环境]\n";
    out << "客户端: " << kAppName << " " << kAppVersion << "\n";
    out << "操作系统: " << (os.empty() ? "未知" : os) << "\n";
    if (cores > 0)
        out << "CPU 逻辑核心: " << cores << "\n";
    out << "内存: " << mem << "\n";
    out << "工作区目录: " << (workspace.empty() ? "未知" : workspace) << "\n";
    //Frozen conversation start date, not the wall clock: a per-turn timestamp
    //would invalidate the provider's prompt cache on every request.
    out << "对话开始: " << dateOf(startedAtMs) << "\n";
    out << "当前模型: " << (model.empty() ? "未设置" : model) << "\n";
    return out.str();
}

} //namespace

const char* defaultSysPrompt()
{
    return defaults::kSys;
}

const char* defaultToolPrompt()
{
    return defaults::kTool;
}

const char* defaultTitlePrompt()
{
    return defaults::kTitle;
}

std::string readFile(const std::filesystem::path& file, const char* fallback)
{
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return fallback ? fallback : "";

    std::string text((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    if (trim(text).empty())
        return fallback ? fallback : "";
    return text;
}

bool writeFile(const std::filesystem::path& file, const std::string& text,
               std::string& err)
{
    std::error_code ec;
    if (!file.parent_path().empty())
        std::filesystem::create_directories(file.parent_path(), ec);
    if (ec)
    {
        err = "无法创建目录：" + paths::toUtf8(file.parent_path());
        return false;
    }

    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        err = "无法写入提示词文件：" + paths::toUtf8(file);
        return false;
    }
    out.write(text.data(), (std::streamsize)text.size());
    if (!out)
    {
        err = "写入提示词文件失败：" + paths::toUtf8(file);
        return false;
    }
    return true;
}

void ensureFiles()
{
    std::string err;
    std::filesystem::path sys = paths::sysPromptFile();
    std::filesystem::path tool = paths::toolPromptFile();
    std::filesystem::path title = paths::titlePromptFile();

    std::error_code ec;
    if (!std::filesystem::exists(sys, ec))
        writeFile(sys, defaults::kSys, err);
    if (!std::filesystem::exists(tool, ec))
        writeFile(tool, defaults::kTool, err);
    if (!std::filesystem::exists(title, ec))
        writeFile(title, defaults::kTitle, err);
}

std::string compose(const std::string& sysText, const std::string& toolText,
                    bool toolsEnabled, const std::string& os,
                    const std::string& workspace, const std::string& model,
                    const std::string& agentsText, uint64_t startedAtMs)
{
    std::string out = trim(sysText).empty() ? std::string(defaults::kSys) : sysText;

    if (toolsEnabled && !paths::isMobile() && !trim(toolText).empty())
    {
        out += "\n\n";
        out += toolText;
    }

    if (toolsEnabled && paths::isMobile())
    {
        out += "\n\n";
        out += tr().mobileToolsNote;
    }

    if (!trim(agentsText).empty())
    {
        out += "\n\n";
        out += agentsText;
    }

    out += "\n\n";
    out += runtimeBlock(os, workspace, model, startedAtMs);
    return out;
}

} //namespace sdlagent::prompt
