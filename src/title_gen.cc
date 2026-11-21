// SPDX-License-Identifier: GPL-3.0-only

#include "title_gen.hpp"

#include "http_transport.hpp"
#include "util.hpp"

#include "llmapi.h"

#include <cstdlib>
#include <cstring>

namespace sdlagent::titlegen
{
namespace
{

bool startsWith(const std::string& s, const char* prefix)
{
    return s.rfind(prefix, 0) == 0;
}

bool endsWith(const std::string& s, const char* suffix)
{
    size_t n = std::strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

void stripPrefix(std::string& s, const char* prefix)
{
    if (startsWith(s, prefix))
        s.erase(0, std::strlen(prefix));
}

void stripSuffix(std::string& s, const char* suffix)
{
    if (endsWith(s, suffix))
        s.erase(s.size() - std::strlen(suffix));
}

std::string cleanTitle(const std::string& raw)
{
    std::string s = trim(raw);

    size_t nl = s.find_first_of("\r\n");
    if (nl != std::string::npos)
        s = trim(s.substr(0, nl));

    stripPrefix(s, "# ");
    stripPrefix(s, "- ");
    stripPrefix(s, "* ");
    stripPrefix(s, "标题:");

    for (int i = 0; i < 3; ++i)
    {
        stripPrefix(s, "\"");
        stripPrefix(s, "'");
        stripPrefix(s, "“");
        stripPrefix(s, "‘");
        stripSuffix(s, "\"");
        stripSuffix(s, "'");
        stripSuffix(s, "”");
        stripSuffix(s, "’");
        stripSuffix(s, "。");
        stripSuffix(s, ".");
        stripSuffix(s, "！");
        stripSuffix(s, "!");
        stripSuffix(s, "？");
        stripSuffix(s, "?");
        stripSuffix(s, "：");
        stripSuffix(s, ":");
        s = trim(s);
    }

    if (utf8Length(s) > 40)
        s = utf8Prefix(s, 40);
    return s;
}

} //namespace

std::string generate(const Request& request, std::atomic<bool>* cancel,
                     std::string& err)
{
    err.clear();
    if (trim(request.baseUrl).empty() || trim(request.apiKey).empty() ||
        trim(request.model).empty())
    {
        err = "API 配置不完整";
        return {};
    }

    std::string user = utf8Prefix(trim(request.userText), 1500);
    std::string assistant = utf8Prefix(trim(request.assistantText), 1500);
    if (user.empty() && assistant.empty())
    {
        err = "没有可用的对话内容";
        return {};
    }

    std::string conversation;
    conversation += "用户:\n";
    conversation += user;
    conversation += "\n\n助手:\n";
    conversation += assistant;

    LlmmapiCtx* ctx = llmapiInit();
    if (!ctx)
    {
        err = "无法初始化 LLM 上下文";
        return {};
    }

    TransportState state;
    state.cancel = cancel;

    LlmmapiTransport transport;
    std::memset(&transport, 0, sizeof(transport));
    transport.perform = &CurlTransport::perform;
    transport.userdata = &state;
    llmapiSetTransport(ctx, &transport);

    LlmmapiReq* req = llmapiReqNew(request.model.c_str());
    if (!req)
    {
        llmapiCleanup(ctx);
        err = "无法创建标题请求";
        return {};
    }

    llmapiReqAddMsg(req, LLMAPI_ROLE_SYSTEM, request.prompt.c_str());
    llmapiReqAddMsg(req, LLMAPI_ROLE_USER, conversation.c_str());
    llmapiReqSetTemperature(req, 0.0);
    llmapiReqSetMaxTokens(req, 128);

    char* response = llmapiChatComplete(ctx, request.baseUrl.c_str(),
                                        request.apiKey.c_str(), req);
    llmapiReqFree(req);

    if (!response)
    {
        const char* message = llmapiStrError(ctx);
        err = (message && *message) ? message : "标题请求失败";
        llmapiCleanup(ctx);
        return {};
    }

    char* content = llmapiRespGetContent(response, 0);
    if (!content)
        content = llmapiRespGetReasoningContent(response, 0);
    std::free(response);
    llmapiCleanup(ctx);

    if (!content)
    {
        err = "标题响应为空";
        return {};
    }

    std::string title = cleanTitle(content);
    std::free(content);

    if (title.empty())
    {
        err = "标题为空";
        return {};
    }
    return title;
}

} //namespace sdlagent::titlegen
