// SPDX-License-Identifier: GPL-3.0-only

#include "util.hpp"

#include <chrono>
#include <cstdio>
#include <random>

namespace sdlagent
{

uint64_t nowMs()
{
    using namespace std::chrono;
    return (uint64_t)duration_cast<milliseconds>(
               system_clock::now().time_since_epoch())
        .count();
}

std::string makeId()
{
    static const char* hex = "0123456789abcdef";
    std::random_device rd;
    std::mt19937_64 gen(((uint64_t)rd() << 32) ^ (uint64_t)rd() ^ nowMs());

    uint64_t r = gen();
    std::string out = std::to_string(nowMs());
    out += '-';
    for (int i = 0; i < 8; ++i)
        out += hex[(r >> (i * 4)) & 0xF];
    return out;
}

std::string trim(const std::string& s)
{
    size_t b = 0;
    size_t e = s.size();
    while (b < e && (unsigned char)s[b] <= ' ')
        ++b;
    while (e > b && (unsigned char)s[e - 1] <= ' ')
        --e;
    return s.substr(b, e - b);
}

std::string toLower(std::string s)
{
    for (char& c : s)
    {
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
    }
    return s;
}

bool startsWith(const std::string& s, const std::string& prefix)
{
    return s.size() >= prefix.size() &&
           s.compare(0, prefix.size(), prefix) == 0;
}

bool endsWith(const std::string& s, const std::string& suffix)
{
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

size_t utf8Length(const std::string& s)
{
    size_t n = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80)
            ++n;
    return n;
}

std::string utf8Prefix(const std::string& s, size_t maxCp)
{
    size_t cp = 0;
    size_t i = 0;
    while (i < s.size() && cp < maxCp)
    {
        unsigned char c = (unsigned char)s[i];
        size_t len = 1;
        if ((c & 0x80) == 0)
            len = 1;
        else if ((c & 0xE0) == 0xC0)
            len = 2;
        else if ((c & 0xF0) == 0xE0)
            len = 3;
        else if ((c & 0xF8) == 0xF0)
            len = 4;
        if (i + len > s.size())
            len = 1;
        i += len;
        ++cp;
    }
    return s.substr(0, i);
}

std::string replaceAll(std::string s, const std::string& from,
                       const std::string& to)
{
    if (from.empty())
        return s;

    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos)
    {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

std::string formatBytes(uint64_t bytes)
{
    char buf[64];
    if (bytes < 1024)
        std::snprintf(buf, sizeof(buf), "%llu B", (unsigned long long)bytes);
    else if (bytes < 1024ull * 1024)
        std::snprintf(buf, sizeof(buf), "%.1f KB", (double)bytes / 1024.0);
    else
        std::snprintf(buf, sizeof(buf), "%.1f MB",
                      (double)bytes / (1024.0 * 1024.0));
    return buf;
}

std::string firstLine(const std::string& s, size_t maxCp)
{
    std::string t = trim(s);
    size_t nl = t.find_first_of("\r\n");
    if (nl != std::string::npos)
        t = t.substr(0, nl);
    t = trim(t);
    if (utf8Length(t) > maxCp)
        t = utf8Prefix(t, maxCp) + "…";
    return t;
}

} //namespace sdlagent
