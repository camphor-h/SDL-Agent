// SPDX-License-Identifier: GPL-3.0-only

#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "app.hpp"
#include "font.hpp"
#include "http_transport.hpp"
#include "log.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

static int SDLCALL appMain(int argc, char** argv)
{
    bool openSettings = false;
    std::string initialSession;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--settings") == 0)
        {
            openSettings = true;
        }
        else if (std::strcmp(argv[i], "--session") == 0 && i + 1 < argc)
        {
            initialSession = argv[++i];
        }
        else if (std::strncmp(argv[i], "--session=", 10) == 0)
        {
            initialSession = argv[i] + 10;
        }
    }

    if (!sdlagent::CurlTransport::globalInit())
        sdlagent::log::writeLine("libcurl global init failed");

    int rc = 0;
    {
        sdlagent::App app(openSettings, std::move(initialSession));
        rc = app.run();
        sdlagent::font::shutdown();
    }

    sdlagent::CurlTransport::globalCleanup();
    return rc;
}

int main(int argc, char** argv)
{
#ifdef _WIN32
    // Console subsystem (debug builds).
    (void)argc;
    (void)argv;
    return SDL_RunApp(0, nullptr, appMain, nullptr);
#else
    return SDL_RunApp(argc, argv, appMain, nullptr);
#endif
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    // GUI subsystem (release builds): no console window.
    return SDL_RunApp(0, nullptr, appMain, nullptr);
}
#endif
