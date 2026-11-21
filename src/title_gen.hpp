// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <atomic>
#include <string>

namespace sdlagent::titlegen
{

struct Request
{
    std::string baseUrl;
    std::string apiKey;
    std::string model;
    std::string prompt;
    std::string userText;
    std::string assistantText;
};

//Generates a short conversation title with a separate non-streaming request.
//Returns an empty string on failure; err receives a human-readable reason.
std::string generate(const Request& request, std::atomic<bool>* cancel,
                     std::string& err);

} //namespace sdlagent::titlegen
