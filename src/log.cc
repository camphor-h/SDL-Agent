// SPDX-License-Identifier: GPL-3.0-only

#include "log.hpp"

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <deque>
#include <fstream>
#include <mutex>
#include <vector>

namespace sdlagent::log
{
namespace
{

constexpr std::size_t kMaxLines = 2000;
constexpr std::size_t kMaxBytes = 256 * 1024;

std::mutex        gMutex;
std::deque<std::string> gLines;
std::size_t       gBytes = 0;
uint64_t          gRevision = 0;

std::string timestamp()
{
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif

    char buf[16];
    if (std::strftime(buf, sizeof(buf), "%H:%M:%S", &tmv) == 0)
        return "--:--:--";
    return buf;
}

void trimLocked()
{
    while (gLines.size() > kMaxLines)
    {
        gBytes -= gLines.front().size() + 1;
        gLines.pop_front();
    }
    while (gBytes > kMaxBytes && !gLines.empty())
    {
        gBytes -= gLines.front().size() + 1;
        gLines.pop_front();
    }
}

} //namespace

void writeLine(const std::string& raw)
{
    std::string text = raw;
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
        text.pop_back();

    std::string line = "[" + timestamp() + "] " + text;

    {
        std::lock_guard<std::mutex> lock(gMutex);
        gLines.push_back(line);
        gBytes += line.size() + 1;
        ++gRevision;
        trimLocked();
    }

    std::fprintf(stderr, "%s\n", line.c_str());
}

void writef(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    va_list copy;
    va_copy(copy, args);
    int needed = std::vsnprintf(nullptr, 0, fmt, copy);
    va_end(copy);

    if (needed < 0)
    {
        va_end(args);
        return;
    }

    std::vector<char> buf((std::size_t)needed + 1);
    std::vsnprintf(buf.data(), buf.size(), fmt, args);
    va_end(args);

    writeLine(std::string(buf.data(), (std::size_t)needed));
}

std::string dump()
{
    std::lock_guard<std::mutex> lock(gMutex);
    std::string out;
    out.reserve(gBytes + gLines.size());
    for (const std::string& line : gLines)
    {
        out += line;
        out += '\n';
    }
    return out;
}

uint64_t revision()
{
    std::lock_guard<std::mutex> lock(gMutex);
    return gRevision;
}

bool exportTo(const std::filesystem::path& path, std::string& err)
{
    err.clear();

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        err = "无法写入日志文件。";
        return false;
    }

    std::string text = dump();
    out.write(text.data(), (std::streamsize)text.size());
    if (!out)
    {
        err = "写入日志文件失败。";
        return false;
    }
    return true;
}

} //namespace sdlagent::log
