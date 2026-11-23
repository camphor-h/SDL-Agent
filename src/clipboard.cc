// SPDX-License-Identifier: GPL-3.0-only

#include "clipboard.hpp"

#include <SDL3/SDL.h>

#include <cctype>
#include <cstddef>
#include <cstring>
#include <cwchar>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>
#endif

namespace sdlagent::clipboard
{
namespace
{

#if defined(_WIN32)
std::string wideToUtf8(const wchar_t* w)
{
    if (!w || !*w)
        return {};

    int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr,
                                  nullptr);
    if (len <= 1)
        return {};

    std::string out((std::size_t)len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), len, nullptr, nullptr);
    return out;
}

//Parses the CF_HDROP payload (DROPFILES header followed by a double-null
//terminated list). Kept separate so it can be unit tested without touching the
//real clipboard.
std::vector<std::string> parseDropFiles(const BYTE* base, std::size_t total)
{
    std::vector<std::string> out;
    if (!base || total < sizeof(DROPFILES))
        return out;

    const DROPFILES* df = reinterpret_cast<const DROPFILES*>(base);
    if (df->pFiles >= total)
        return out;

    const BYTE* p = base + df->pFiles;
    const BYTE* end = base + total;

    if (df->fWide)
    {
        const wchar_t* w = reinterpret_cast<const wchar_t*>(p);
        while ((const BYTE*)(w + 1) <= end && *w)
        {
            std::string path = wideToUtf8(w);
            if (!path.empty())
                out.push_back(std::move(path));
            w += std::wcslen(w) + 1;
        }
    }
    else
    {
        const char* s = reinterpret_cast<const char*>(p);
        while ((const BYTE*)s < end && *s)
        {
            out.push_back(s);
            s += std::strlen(s) + 1;
        }
    }

    return out;
}

//Explorer copies files as CF_HDROP. SDL's clipboard API does not expose that
//format, so read it directly.
std::vector<std::string> winFiles()
{
    if (!IsClipboardFormatAvailable(CF_HDROP))
        return {};
    if (!OpenClipboard(nullptr))
        return {};

    std::vector<std::string> out;
    if (HANDLE handle = GetClipboardData(CF_HDROP))
    {
        const std::size_t total = GlobalSize(handle);
        const BYTE* base = static_cast<const BYTE*>(GlobalLock(handle));
        if (base)
        {
            out = parseDropFiles(base, total);
            GlobalUnlock(handle);
        }
    }

    CloseClipboard();
    return out;
}
#endif

std::string percentDecode(const std::string& in)
{
    std::string out;
    out.reserve(in.size());

    for (std::size_t i = 0; i < in.size(); ++i)
    {
        if (in[i] == '%' && i + 2 < in.size() &&
            std::isxdigit((unsigned char)in[i + 1]) &&
            std::isxdigit((unsigned char)in[i + 2]))
        {
            auto hex = [](char c) -> int
            {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                return c - 'A' + 10;
            };
            out.push_back((char)((hex(in[i + 1]) << 4) | hex(in[i + 2])));
            i += 2;
        }
        else
        {
            out.push_back(in[i]);
        }
    }
    return out;
}

//Minimal file:// URI to local path conversion. SDL3 does not provide
//SDL_URIToLocal, and the uri-list format is simple enough to handle here.
std::string uriToLocal(const std::string& uri)
{
    if (uri.rfind("file://", 0) != 0)
        return {};

    std::string path = uri.substr(7);

    //Optional authority: file://localhost/home/x
    if (!path.empty() && path[0] != '/')
    {
        std::size_t slash = path.find('/');
        std::string host = path.substr(0, slash);
        if (host != "localhost")
            return {};
        path = (slash == std::string::npos) ? "/" : path.substr(slash);
    }

    path = percentDecode(path);

#if defined(_WIN32)
    //file:///C:/dir -> C:/dir
    if (path.size() >= 3 && path[0] == '/' &&
        std::isalpha((unsigned char)path[1]) && path[2] == ':')
        path.erase(0, 1);
#endif

    return path;
}

//X11/Wayland clipboards expose files as text/uri-list. On Windows this is
//normally empty (CF_HDROP is handled above).
std::vector<std::string> uriListFiles()
{
    std::vector<std::string> out;
    if (!SDL_HasClipboardData("text/uri-list"))
        return out;

    std::size_t size = 0;
    void* data = SDL_GetClipboardData("text/uri-list", &size);
    if (!data)
        return out;

    std::string text(static_cast<const char*>(data), size);
    SDL_free(data);

    std::size_t start = 0;
    while (start < text.size())
    {
        std::size_t end = text.find_first_of("\r\n", start);
        if (end == std::string::npos)
            end = text.size();

        std::string line = text.substr(start, end - start);
        start = end + 1;

        if (line.empty() || line[0] == '#')
            continue;
        if (line.rfind("file://", 0) != 0)
            continue;

        std::string local = uriToLocal(line);
        if (!local.empty())
            out.push_back(std::move(local));
    }

    return out;
}

} //namespace

std::vector<std::string> files()
{
#if defined(_WIN32)
    std::vector<std::string> out = winFiles();
    if (!out.empty())
        return out;
#endif
    return uriListFiles();
}

} //namespace sdlagent::clipboard
