// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "http_header.hpp"

#include <atomic>
#include <string>
#include <vector>

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
    std::string sessionId;
    std::string apiProtocol = "openai";
    std::vector<HttpHeader> headers;
};

//Generates a short conversation title with a separate non-streaming request.
//Returns an empty string on failure; err receives a human-readable reason.
std::string generate(const Request& request, std::atomic<bool>* cancel,
                     std::string& err);

} //namespace sdlagent::titlegen
