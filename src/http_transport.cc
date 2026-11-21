// SPDX-License-Identifier: GPL-3.0-only

#include "http_transport.hpp"

#include "ca_bundle.hpp"
#include "version.hpp"

#include <curl/curl.h>

#include <cstring>
#include <string>

namespace sdlagent
{
namespace
{

//the transport result only borrows the error string, so keep it alive until
//the next call on this thread
thread_local std::string tlsError;

struct WriteCtx
{
    LlmmapiWriteFn onChunk = nullptr;
    void*          userdata = nullptr;
    std::atomic<bool>* cancel = nullptr;
    bool aborted = false;
};

size_t writeCallback(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    WriteCtx* ctx = static_cast<WriteCtx*>(userdata);
    size_t bytes = size * nmemb;

    if (ctx->cancel && ctx->cancel->load(std::memory_order_relaxed))
    {
        ctx->aborted = true;
        return 0;
    }

    if (!ctx->onChunk)
        return bytes;

    if (!ctx->onChunk(ptr, bytes, ctx->userdata))
    {
        ctx->aborted = true;
        return 0;
    }
    return bytes;
}

int progressCallback(void* userdata, curl_off_t, curl_off_t, curl_off_t,
                     curl_off_t)
{
    TransportState* st = static_cast<TransportState*>(userdata);
    if (st->cancel && st->cancel->load(std::memory_order_relaxed))
        return 1;
    return 0;
}

} //namespace

bool CurlTransport::globalInit()
{
    return curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
}

void CurlTransport::globalCleanup()
{
    curl_global_cleanup();
}

int CurlTransport::perform(void* userdata,
                           const LlmmapiHttpRequest* request,
                           LlmmapiWriteFn onChunk,
                           void* chunkUserdata,
                           LlmmapiHttpResult* result)
{
    if (!request || !result)
        return 1;

    result->status = 0;
    result->error = nullptr;

    TransportState* state = static_cast<TransportState*>(userdata);

    CURL* curl = curl_easy_init();
    if (!curl)
    {
        tlsError = "无法初始化 libcurl";
        result->error = tlsError.c_str();
        return 1;
    }

    curl_slist* headers = nullptr;
    if (request->headers)
    {
        for (const char* const* h = request->headers; *h; ++h)
            headers = curl_slist_append(headers, *h);
    }

    if (state)
    {
        for (const HttpHeader& header : state->headers)
        {
            std::string line = header.name + ": " + header.value;
            headers = curl_slist_append(headers, line.c_str());
        }
    }

    WriteCtx write;
    write.onChunk = onChunk;
    write.userdata = chunkUserdata;
    write.cancel = state ? state->cancel : nullptr;

    char errbuf[CURL_ERROR_SIZE];
    errbuf[0] = '\0';

    curl_easy_setopt(curl, CURLOPT_URL, request->url);
    ca::apply(curl);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, (const char*)request->body);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE,
                     (curl_off_t)request->bodyLen);
    if (headers)
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &write);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    std::string userAgent = std::string("SDLAgent/") + kAppVersion;
    curl_easy_setopt(curl, CURLOPT_USERAGENT, userAgent.c_str());
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 30000L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 180L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progressCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, state);

    CURLcode rc = curl_easy_perform(curl);

    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    result->status = status;

    if (headers)
        curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK)
    {
        if (write.aborted ||
            (state && state->cancel &&
             state->cancel->load(std::memory_order_relaxed)))
            tlsError = "请求已取消";
        else if (errbuf[0])
            tlsError = errbuf;
        else
            tlsError = curl_easy_strerror(rc);

        result->error = tlsError.c_str();
        return 1;
    }

    return 0;
}

} //namespace sdlagent
