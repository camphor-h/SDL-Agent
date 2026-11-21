// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <atomic>
#include <filesystem>
#include <string>

namespace sdlagent::apiupdate
{

struct Request
{
    std::string url;
    int         proxyPort = 0; //0 = no proxy
    std::filesystem::path target;
};

struct Result
{
    bool        ok = false;
    bool        cancelled = false;
    std::filesystem::path savedPath;
    std::string error;
};

//Blocking download. Writes to <target>.download first, validates the JSON,
//then replaces target. On any failure the partial file is left in place.
Result download(const Request& request, std::atomic<int>& percent,
                std::atomic<bool>& cancel);

} //namespace sdlagent::apiupdate
