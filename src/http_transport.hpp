// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "http_header.hpp"
#include "llmapi.h"

#include <atomic>
#include <string>
#include <vector>

namespace sdlagent
{

//state shared between the agent worker and the curl transport
struct TransportState
{
    std::atomic<bool>* cancel = nullptr;
    //Stable conversation ID and per-provider headers.
    std::string sessionId;
    std::vector<HttpHeader> headers;
};

//libcurl based transport for libllmapi. Streaming responses are forwarded to
//the chunk callback as they arrive.
class CurlTransport
{
public:
    static bool globalInit();
    static void globalCleanup();

    static int perform(void* userdata,
                       const LlmmapiHttpRequest* request,
                       LlmmapiWriteFn onChunk,
                       void* chunkUserdata,
                       LlmmapiHttpResult* result);
};

} //namespace sdlagent
