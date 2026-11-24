// SPDX-License-Identifier: GPL-3.0-only

#include "agents.hpp"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace sdlagent::agents
{
namespace
{

std::string readText(const std::filesystem::path& file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return {};

    std::string text((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());

    //Trim leading/trailing whitespace-only lines; keep the body intact.
    std::size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    std::size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::string hashText(const std::string& text)
{
    std::uint64_t h = 1469598103934665603ull;
    for (unsigned char c : text)
    {
        h ^= (std::uint64_t)c;
        h *= 1099511628211ull;
    }

    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx",
                  (unsigned long long)h);
    return std::string(buf);
}

} //namespace

Snapshot load(const std::filesystem::path& globalFile,
              const std::filesystem::path& projectFile)
{
    Snapshot snap;

    const std::string global = readText(globalFile);
    const std::string project = readText(projectFile);

    if (!global.empty() && !project.empty())
    {
        snap.text =
            "## 全局 AGENTS.md\n" + global +
            "\n\n若以下项目规则与全局规则冲突，以项目规则为准。\n\n"
            "## 项目 AGENTS.md\n" + project;
    }
    else if (!global.empty())
    {
        snap.text = global;
    }
    else if (!project.empty())
    {
        snap.text = project;
    }

    snap.hash = hashText(snap.text);
    return snap;
}

} //namespace sdlagent::agents
